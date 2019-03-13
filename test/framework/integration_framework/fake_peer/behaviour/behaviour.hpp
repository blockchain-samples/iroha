/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INTEGRATION_FRAMEWORK_FAKE_PEER_BEHAVIOUR_HPP_
#define INTEGRATION_FRAMEWORK_FAKE_PEER_BEHAVIOUR_HPP_

#include <functional>
#include <memory>
#include <vector>

#include "framework/integration_framework/fake_peer/fake_peer.hpp"
#include "framework/integration_framework/fake_peer/types.hpp"
#include "logger/logger_fwd.hpp"

namespace shared_model {
  namespace proto {
    class Block;
  }
}  // namespace shared_model

namespace integration_framework {
  namespace fake_peer {

    class Behaviour {
     public:
      virtual ~Behaviour();

      /// Enable the behaviour for the given peer and take the given logger.
      void setup(const std::shared_ptr<FakePeer> &fake_peer,
                 logger::LoggerPtr log);

      /// Disable the behaviour
      void absolve();

      /// This method gets subscribed on Fake Peer's MST messages.
      virtual void processMstMessage(std::shared_ptr<MstMessage> message) = 0;

      /// This method gets subscribed on Fake Peer's YAC messages.
      virtual void processYacMessage(
          std::shared_ptr<const YacMessage> message) = 0;

      /// This method gets subscribed on Fake Peer's OS messages.
      virtual void processOsBatch(
          std::shared_ptr<shared_model::interface::TransactionBatch> batch) = 0;

      /// This method gets subscribed on Fake Peer's OG messages.
      virtual void processOgProposal(
          std::shared_ptr<shared_model::interface::Proposal> proposal) = 0;

      /// This method handles block requests for Fake Peer's.
      virtual LoaderBlockRequestResult processLoaderBlockRequest(
          LoaderBlockRequest request) = 0;

      /// This method handles blocks requests for Fake Peer's.
      virtual LoaderBlocksRequestResult processLoaderBlocksRequest(
          LoaderBlocksRequest request) = 0;

      /// serve the proposal request
      virtual OrderingProposalRequestResult processOrderingProposalRequest(
          const OrderingProposalRequest &request) = 0;

      /// process the batches submitted to a fake peer's on demand OS
      virtual void processOrderingBatches(
          const BatchesForRound &batches_for_round) = 0;

     protected:
      FakePeer &getFakePeer();
      logger::LoggerPtr &getLogger();

     private:
      std::weak_ptr<FakePeer> fake_peer_wptr_;
      std::vector<rxcpp::subscription> subscriptions_;
      logger::LoggerPtr log_;
    };

  }  // namespace fake_peer
}  // namespace integration_framework

#endif /* INTEGRATION_FRAMEWORK_FAKE_PEER_BEHAVIOUR_HPP_ */
