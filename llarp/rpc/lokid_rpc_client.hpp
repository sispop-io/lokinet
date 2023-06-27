#pragma once

#include <llarp/router_id.hpp>

#include <sispopmq/sispopmq.h>
#include <sispopmq/address.h>
#include <llarp/crypto/types.hpp>
#include <llarp/dht/key.hpp>
#include <llarp/service/name.hpp>

namespace llarp
{
  struct AbstractRouter;

  namespace rpc
  {
    using LMQ_ptr = std::shared_ptr<sispopmq::SispopMQ>;

    /// The LokidRpcClient uses loki-mq to talk to make API requests to lokid.
    struct LokidRpcClient : public std::enable_shared_from_this<LokidRpcClient>
    {
      explicit LokidRpcClient(LMQ_ptr lmq, std::weak_ptr<AbstractRouter> r);

      /// Connect to lokid async
      void
      ConnectAsync(sispopmq::address url);

      /// blocking request identity key from lokid
      /// throws on failure
      SecretKey
      ObtainIdentityKey();

      /// get what the current block height is according to sispopd
      uint64_t
      BlockHeight() const
      {
        return m_BlockHeight;
      }

      void
      LookupLNSNameHash(
          dht::Key_t namehash,
          std::function<void(std::optional<service::EncryptedName>)> resultHandler);

      /// inform that if connected to a router successfully
      void
      InformConnection(RouterID router, bool success);

     private:
      /// called when we have connected to lokid via lokimq
      void
      Connected();

      /// do a lmq command on the current connection
      void
      Command(std::string_view cmd);

      /// triggers a service node list refresh from sispopd; thread-safe and will do nothing if an
      /// update is already in progress.
      void
      UpdateServiceNodeList();

      template <typename HandlerFunc_t, typename Args_t>
      void
      Request(std::string_view cmd, HandlerFunc_t func, const Args_t& args)
      {
        m_lokiMQ->request(*m_Connection, std::move(cmd), std::move(func), args);
      }

      template <typename HandlerFunc_t>
      void
      Request(std::string_view cmd, HandlerFunc_t func)
      {
        m_lokiMQ->request(*m_Connection, std::move(cmd), std::move(func));
      }

      // Handles a service node list update; takes the "service_node_states" object of an sispopd
      // "get_service_nodes" rpc request.
      void
      HandleNewServiceNodeList(const nlohmann::json& json);

      // Handles request from lokid for peer stats on a specific peer
      void
      HandleGetPeerStats(sispopmq::Message& msg);

      // Handles notification of a new block
      void
      HandleNewBlock(sispopmq::Message& msg);

      std::optional<sispopmq::ConnectionID> m_Connection;
      LMQ_ptr m_lokiMQ;

      std::weak_ptr<AbstractRouter> m_Router;
      std::atomic<bool> m_UpdatingList;
      std::string m_LastUpdateHash;

      std::unordered_map<RouterID, PubKey> m_KeyMap;

      uint64_t m_BlockHeight;
    };

  }  // namespace rpc
}  // namespace llarp
