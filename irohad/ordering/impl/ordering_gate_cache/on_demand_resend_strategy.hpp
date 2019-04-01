/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_ON_DEMAND_RESEND_STRATEGY_HPP
#define IROHA_ON_DEMAND_RESEND_STRATEGY_HPP

#include "ordering/impl/ordering_gate_cache/ordering_gate_resend_strategy.hpp"

#include <shared_mutex>
#include <unordered_map>

namespace iroha {
  namespace ordering {
    class OnDemandResendStrategy : public OrderingGateResendStrategy {
     public:
      bool feed(std::shared_ptr<shared_model::interface::TransactionBatch>
                    batch) override;

      bool readyToUse(std::shared_ptr<shared_model::interface::TransactionBatch>
                          batch) override;

      RoundSetType extract(
          std::shared_ptr<shared_model::interface::TransactionBatch> batch)
          override;

      void remove(
          const cache::OrderingGateCache::HashesSetType &hashes) override;

      void setCurrentRound(const consensus::Round &current_round) override;

      consensus::Round getCurrentRound() const override;

     private:
      std::unordered_map<
          std::shared_ptr<shared_model::interface::TransactionBatch>,
          std::pair<consensus::Round, bool>>
          sent_batches_;
      consensus::Round current_round_;
      mutable std::shared_timed_mutex access_mutex_;
    };
  }  // namespace ordering
}  // namespace iroha

#endif  // IROHA_ON_DEMAND_RESEND_STRATEGY_HPP
