/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ordering/impl/on_demand_ordering_gate.hpp"

#include <iterator>

#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/empty.hpp>
#include "ametsuchi/tx_presence_cache.hpp"
#include "common/visitor.hpp"
#include "cryptography/public_key.hpp"
#include "interfaces/iroha_internal/transaction_batch.hpp"
#include "interfaces/iroha_internal/transaction_batch_parser_impl.hpp"
#include "logger/logger.hpp"
#include "ordering/impl/on_demand_common.hpp"

using namespace iroha;
using namespace iroha::ordering;

std::string OnDemandOrderingGate::BlockEvent::toString() const {
  return shared_model::detail::PrettyStringBuilder()
      .init("BlockEvent")
      .append(round.toString())
      .finalize();
}

std::string OnDemandOrderingGate::EmptyEvent::toString() const {
  return shared_model::detail::PrettyStringBuilder()
      .init("EmptyEvent")
      .append(round.toString())
      .finalize();
}

OnDemandOrderingGate::OnDemandOrderingGate(
    std::shared_ptr<OnDemandOrderingService> ordering_service,
    std::shared_ptr<transport::OdOsNotification> network_client,
    rxcpp::observable<BlockRoundEventType> events,
    std::shared_ptr<cache::OrderingGateCache> cache,
    std::shared_ptr<shared_model::interface::UnsafeProposalFactory> factory,
    std::shared_ptr<ametsuchi::TxPresenceCache> tx_cache,
    std::shared_ptr<ProposalCreationStrategy> proposal_creation_strategy,
    size_t transaction_limit,
    logger::LoggerPtr log)
    : log_(std::move(log)),
      transaction_limit_(transaction_limit),
      ordering_service_(std::move(ordering_service)),
      network_client_(std::move(network_client)),
      events_subscription_(events.subscribe([this](auto event) {
        consensus::Round current_round =
            visit_in_place(event, [this, &current_round](const auto &event) {
              log_->debug("{}", event);
              return event.round;
            });

        auto ledger_state =
            visit_in_place(event, [this, &current_round](const auto &event) {
              return event.ledger_state;
            });

        auto peer_keys = [](const auto peers) {
          OnDemandOrderingService::PeerList lst;
          for (auto peer : peers) {
            auto shp_pub_key =
                std::make_shared<shared_model::crypto::PublicKey>(peer->pubkey());
            lst.push_back(shp_pub_key);
          }
          return lst;
        };
        // notify our ordering service about new round
        proposal_creation_strategy_->onCollaborationOutcome(
            *ledger_state->ledger_peers
            | boost::adaptors::transformed(
                  [](auto &peer) -> decltype(auto) { return peer->pubkey(); }));
        ordering_service_->onCollaborationOutcome(
            current_round, peer_keys(*ledger_state->ledger_peers));

        this->sendCachedTransactions(event);

        // request proposal for the current round
        auto proposal = this->processProposalRequest(
            network_client_->onRequestProposal(current_round));
        // vote for the object received from the network
        proposal_notifier_.get_subscriber().on_next(
            network::OrderingEvent{std::move(proposal), current_round});
      })),
      cache_(std::move(cache)),
      proposal_factory_(std::move(factory)),
      tx_cache_(std::move(tx_cache)),
      proposal_creation_strategy_(std::move(proposal_creation_strategy)) {}

OnDemandOrderingGate::~OnDemandOrderingGate() {
  events_subscription_.unsubscribe();
}

void OnDemandOrderingGate::propagateBatch(
    std::shared_ptr<shared_model::interface::TransactionBatch> batch) {
  cache_->addToBack({batch});

  network_client_->onBatches(
      transport::OdOsNotification::CollectionType{batch});
}

rxcpp::observable<network::OrderingEvent> OnDemandOrderingGate::onProposal() {
  return proposal_notifier_.get_observable();
}

boost::optional<std::shared_ptr<const shared_model::interface::Proposal>>
OnDemandOrderingGate::processProposalRequest(
    boost::optional<
        std::shared_ptr<const OnDemandOrderingService::ProposalType>> proposal)
    const {
  if (not proposal) {
    return boost::none;
  }
  auto proposal_without_replays = removeReplays(*std::move(proposal));
  // no need to check empty proposal
  if (boost::empty(proposal_without_replays->transactions())) {
    return boost::none;
  }
  return proposal_without_replays;
}

void OnDemandOrderingGate::sendCachedTransactions(
    const BlockRoundEventType &event) {
  visit_in_place(event,
                 [this](const BlockEvent &block_event) {
                   // block committed, remove transactions from cache
                   cache_->remove(block_event.hashes);
                 },
                 [](const EmptyEvent &) {
                   // no blocks committed, no transactions to remove
                 });

  auto batches = cache_->pop();
  cache_->addToBack(batches);

  // get only transactions which fit to next proposal
  auto end_iterator = batches.begin();
  auto current_number_of_transactions = 0u;
  for (; end_iterator != batches.end(); ++end_iterator) {
    auto batch_size = (*end_iterator)->transactions().size();
    if (current_number_of_transactions + batch_size <= transaction_limit_) {
      current_number_of_transactions += batch_size;
    } else {
      break;
    }
  }

  if (not batches.empty()) {
    network_client_->onBatches(transport::OdOsNotification::CollectionType{
        batches.begin(), end_iterator});
  }
}

std::shared_ptr<const shared_model::interface::Proposal>
OnDemandOrderingGate::removeReplays(
    std::shared_ptr<const shared_model::interface::Proposal> proposal) const {
  std::vector<bool> proposal_txs_validation_results;
  auto tx_is_not_processed = [this](const auto &tx) {
    auto tx_result = tx_cache_->check(tx.hash());
    if (not tx_result) {
      // TODO andrei 30.11.18 IR-51 Handle database error
      return false;
    }
    return iroha::visit_in_place(
        *tx_result,
        [](const ametsuchi::tx_cache_status_responses::Missing &) {
          return true;
        },
        [](const auto &status) {
          // TODO nickaleks 21.11.18: IR-1887 log replayed transactions
          // when log is added
          return false;
        });
  };

  shared_model::interface::TransactionBatchParserImpl batch_parser;

  bool has_replays = false;
  auto batches = batch_parser.parseBatches(proposal->transactions());
  for (auto &batch : batches) {
    bool all_txs_are_new =
        std::all_of(batch.begin(), batch.end(), tx_is_not_processed);
    proposal_txs_validation_results.insert(
        proposal_txs_validation_results.end(), batch.size(), all_txs_are_new);
    has_replays |= not all_txs_are_new;
  }

  if (not has_replays) {
    return std::move(proposal);
  }

  auto unprocessed_txs =
      proposal->transactions() | boost::adaptors::indexed()
      | boost::adaptors::filtered(
            [proposal_txs_validation_results =
                 std::move(proposal_txs_validation_results)](const auto &el) {
              return proposal_txs_validation_results.at(el.index());
            })
      | boost::adaptors::transformed(
            [](const auto &el) -> decltype(auto) { return el.value(); });

  return proposal_factory_->unsafeCreateProposal(
      proposal->height(), proposal->createdTime(), unprocessed_txs);
}
