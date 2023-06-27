#pragma once

#include <string_view>
#include <sispopmq/sispopmq.h>
#include <sispopmq/address.h>
#include <sispop/log/omq_logger.hpp>

namespace llarp
{
  struct AbstractRouter;
}

namespace llarp::rpc
{
  using LMQ_ptr = std::shared_ptr<sispopmq::SispopMQ>;

  struct RpcServer
  {
    explicit RpcServer(LMQ_ptr, AbstractRouter*);
    ~RpcServer() = default;
    void
    AsyncServeRPC(sispopmq::address addr);

   private:
    void
    HandleLogsSubRequest(sispopmq::Message& m);

    LMQ_ptr m_LMQ;
    AbstractRouter* const m_Router;

    sispop::log::PubsubLogger log_subs;
  };
}  // namespace llarp::rpc
