#include "rpc_server.hpp"
#include <llarp/router/route_poker.hpp>
#include <llarp/constants/platform.hpp>
#include <llarp/constants/version.hpp>
#include <nlohmann/json.hpp>
#include <llarp/exit/context.hpp>
#include <llarp/net/ip_range.hpp>
#include <llarp/quic/tunnel.hpp>
#include <llarp/service/context.hpp>
#include <llarp/service/outbound_context.hpp>
#include <llarp/service/auth.hpp>
#include <llarp/service/name.hpp>
#include <llarp/router/abstractrouter.hpp>
#include <llarp/dns/dns.hpp>
#include <sispopmq/fmt.h>

namespace
{
  static auto logcat = llarp::log::Cat("lokinet.rpc");
}  // namespace

namespace llarp::rpc
{
  RpcServer::RpcServer(LMQ_ptr lmq, AbstractRouter* r)
      : m_LMQ{std::move(lmq)}, m_Router{r}, log_subs{*m_LMQ, llarp::logRingBuffer}
  {}

  /// maybe parse json from message paramter at index
  std::optional<nlohmann::json>
  MaybeParseJSON(const sispopmq::Message& msg, size_t index = 0)
  {
    try
    {
      const auto& str = msg.data.at(index);
      return nlohmann::json::parse(str);
    }
    catch (std::exception&)
    {
      return std::nullopt;
    }
  }

  template <typename Result_t>
  std::string
  CreateJSONResponse(Result_t result)
  {
    const auto obj = nlohmann::json{
        {"error", nullptr},
        {"result", result},
    };
    return obj.dump();
  }

  std::string
  CreateJSONError(std::string_view msg)
  {
    const auto obj = nlohmann::json{
        {"error", msg},
    };
    return obj.dump();
  }

  /// fake packet source that serializes repsonses back into dns

  class DummyPacketSource : public dns::PacketSource_Base
  {
    std::function<void(std::optional<dns::Message>)> func;

   public:
    SockAddr dumb;

    template <typename Callable>
    DummyPacketSource(Callable&& f) : func{std::forward<Callable>(f)}
    {}

    bool
    WouldLoop(const SockAddr&, const SockAddr&) const override
    {
      return false;
    };

    /// send packet with src and dst address containing buf on this packet source
    void
    SendTo(const SockAddr&, const SockAddr&, OwnedBuffer buf) const override
    {
      func(dns::MaybeParseDNSMessage(buf));
    }

    /// stop reading packets and end operation
    void
    Stop() override{};

    /// returns the sockaddr we are bound on if applicable
    std::optional<SockAddr>
    BoundOn() const override
    {
      return std::nullopt;
    }
  };

  /// a function that replies to an rpc request
  using ReplyFunction_t = std::function<void(std::string)>;

  std::shared_ptr<EndpointBase>
  GetEndpointByName(AbstractRouter* r, std::string name)
  {
    if (r->IsServiceNode())
    {
      return r->exitContext().GetExitEndpoint(name);
    }
    else
    {
      return r->hiddenServiceContext().GetEndpointByName(name);
    }
  }

  void
  HandleJSONRequest(
      sispopmq::Message& msg, std::function<void(nlohmann::json, ReplyFunction_t)> handleRequest)
  {
    const auto maybe = MaybeParseJSON(msg);
    if (not maybe.has_value())
    {
      msg.send_reply(CreateJSONError("failed to parse json"));
      return;
    }
    if (not maybe->is_object())
    {
      msg.send_reply(CreateJSONError("request data not a json object"));
      return;
    }
    try
    {
      handleRequest(
          *maybe, [defer = msg.send_later()](std::string result) { defer.reply(result); });
    }
    catch (std::exception& ex)
    {
      msg.send_reply(CreateJSONError(ex.what()));
    }
  }

  void
  RpcServer::AsyncServeRPC(sispopmq::address url)
  {
    m_LMQ->listen_plain(url.zmq_address());
    m_LMQ->add_category("llarp", sispopmq::AuthLevel::none)
        .add_request_command("logs", [this](sispopmq::Message& msg) { HandleLogsSubRequest(msg); })
        .add_request_command(
            "halt",
            [&](sispopmq::Message& msg) {
              if (not m_Router->IsRunning())
              {
                msg.send_reply(CreateJSONError("router is not running"));
                return;
              }
              msg.send_reply(CreateJSONResponse("OK"));
              m_Router->Stop();
            })
        .add_request_command(
            "version",
            [r = m_Router](sispopmq::Message& msg) {
              util::StatusObject result{
                  {"version", llarp::VERSION_FULL}, {"uptime", to_json(r->Uptime())}};
              msg.send_reply(CreateJSONResponse(result));
            })
        .add_request_command(
            "status",
            [&](sispopmq::Message& msg) {
              m_Router->loop()->call([defer = msg.send_later(), r = m_Router]() {
                std::string data;
                if (r->IsRunning())
                {
                  data = CreateJSONResponse(r->ExtractStatus());
                }
                else
                {
                  data = CreateJSONError("router not yet ready");
                }
                defer.reply(data);
              });
            })
        .add_request_command(
            "get_status",
            [&](sispopmq::Message& msg) {
              m_Router->loop()->call([defer = msg.send_later(), r = m_Router]() {
                defer.reply(CreateJSONResponse(r->ExtractSummaryStatus()));
              });
            })
        .add_request_command(
            "quic_connect",
            [&](sispopmq::Message& msg) {
              HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
                std::string endpoint = "default";
                if (auto itr = obj.find("endpoint"); itr != obj.end())
                  endpoint = itr->get<std::string>();

                std::string bindAddr = "127.0.0.1:0";
                if (auto itr = obj.find("bind"); itr != obj.end())
                  bindAddr = itr->get<std::string>();

                std::string remoteHost;
                if (auto itr = obj.find("host"); itr != obj.end())
                  remoteHost = itr->get<std::string>();

                uint16_t port = 0;
                if (auto itr = obj.find("port"); itr != obj.end())
                  port = itr->get<uint16_t>();

                int closeID = 0;
                if (auto itr = obj.find("close"); itr != obj.end())
                  closeID = itr->get<int>();

                if (port == 0 and closeID == 0)
                {
                  reply(CreateJSONError("port not provided"));
                  return;
                }
                if (remoteHost.empty() and closeID == 0)
                {
                  reply(CreateJSONError("host not provided"));
                  return;
                }
                SockAddr laddr{};
                laddr.fromString(bindAddr);

                r->loop()->call([reply, endpoint, r, remoteHost, port, closeID, laddr]() {
                  auto ep = GetEndpointByName(r, endpoint);
                  if (not ep)
                  {
                    reply(CreateJSONError("no such local endpoint"));
                    return;
                  }
                  auto quic = ep->GetQUICTunnel();
                  if (not quic)
                  {
                    reply(CreateJSONError("local endpoint has no quic tunnel"));
                    return;
                  }
                  if (closeID)
                  {
                    quic->close(closeID);
                    reply(CreateJSONResponse("OK"));
                    return;
                  }

                  try
                  {
                    auto [addr, id] = quic->open(
                        remoteHost, port, [](auto&&) {}, laddr);
                    util::StatusObject status;
                    status["addr"] = addr.ToString();
                    status["id"] = id;
                    reply(CreateJSONResponse(status));
                  }
                  catch (std::exception& ex)
                  {
                    reply(CreateJSONError(ex.what()));
                  }
                });
              });
            })
        .add_request_command(
            "quic_listener",
            [&](sispopmq::Message& msg) {
              HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
                std::string endpoint = "default";
                if (auto itr = obj.find("endpoint"); itr != obj.end())
                  endpoint = itr->get<std::string>();

                std::string remote = "127.0.0.1";
                if (auto itr = obj.find("host"); itr != obj.end())
                  remote = itr->get<std::string>();

                uint16_t port = 0;
                if (auto itr = obj.find("port"); itr != obj.end())
                  port = itr->get<uint16_t>();

                int closeID = 0;
                if (auto itr = obj.find("close"); itr != obj.end())
                  closeID = itr->get<int>();

                std::string srvProto;
                if (auto itr = obj.find("srv-proto"); itr != obj.end())
                  srvProto = itr->get<std::string>();

                if (port == 0 and closeID == 0)
                {
                  reply(CreateJSONError("invalid arguments"));
                  return;
                }
                r->loop()->call([reply, endpoint, remote, port, r, closeID, srvProto]() {
                  auto ep = GetEndpointByName(r, endpoint);
                  if (not ep)
                  {
                    reply(CreateJSONError("no such local endpoint"));
                    return;
                  }
                  auto quic = ep->GetQUICTunnel();
                  if (not quic)
                  {
                    reply(CreateJSONError("no quic interface available on endpoint " + endpoint));
                    return;
                  }
                  if (port)
                  {
                    int id = 0;
                    try
                    {
                      SockAddr addr{remote + ":" + std::to_string(port)};
                      id = quic->listen(addr);
                    }
                    catch (std::exception& ex)
                    {
                      reply(CreateJSONError(ex.what()));
                      return;
                    }
                    util::StatusObject result;
                    result["id"] = id;
                    std::string localAddress;
                    var::visit(
                        [&](auto&& addr) { localAddress = addr.ToString(); }, ep->LocalAddress());
                    result["addr"] = localAddress + ":" + std::to_string(port);
                    if (not srvProto.empty())
                    {
                      auto srvData =
                          dns::SRVData::fromTuple(std::make_tuple(srvProto, 1, 1, port, ""));
                      ep->PutSRVRecord(std::move(srvData));
                    }
                    reply(CreateJSONResponse(result));
                  }
                  else if (closeID)
                  {
                    quic->forget(closeID);
                    reply(CreateJSONResponse("OK"));
                  }
                });
              });
            })
        .add_request_command(
            "lookup_snode",
            [&](sispopmq::Message& msg) {
              HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
                if (not r->IsServiceNode())
                {
                  reply(CreateJSONError("not supported"));
                  return;
                }
                RouterID routerID;
                if (auto itr = obj.find("snode"); itr != obj.end())
                {
                  std::string remote = itr->get<std::string>();
                  if (not routerID.FromString(remote))
                  {
                    reply(CreateJSONError("invalid remote: " + remote));
                    return;
                  }
                }
                else
                {
                  reply(CreateJSONError("no remote provided"));
                  return;
                }
                std::string endpoint = "default";
                r->loop()->call([r, endpoint, routerID, reply]() {
                  auto ep = r->exitContext().GetExitEndpoint(endpoint);
                  if (ep == nullptr)
                  {
                    reply(CreateJSONError("cannot find local endpoint: " + endpoint));
                    return;
                  }
                  ep->ObtainSNodeSession(routerID, [routerID, ep, reply](auto session) {
                    if (session and session->IsReady())
                    {
                      const auto ip = net::TruncateV6(ep->GetIPForIdent(PubKey{routerID}));
                      util::StatusObject status{{"ip", ip.ToString()}};
                      reply(CreateJSONResponse(status));
                    }
                    else
                      reply(CreateJSONError("failed to obtain snode session"));
                  });
                });
              });
            })
        .add_request_command(
            "endpoint",
            [&](sispopmq::Message& msg) {
              HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
                if (r->IsServiceNode())
                {
                  reply(CreateJSONError("not supported"));
                  return;
                }
                std::string endpoint = "default";
                std::unordered_set<service::Address> kills;
                {
                  const auto itr = obj.find("endpoint");
                  if (itr != obj.end())
                    endpoint = itr->get<std::string>();
                }
                {
                  const auto itr = obj.find("kill");
                  if (itr != obj.end())
                  {
                    if (itr->is_array())
                    {
                      for (auto kill_itr = itr->begin(); kill_itr != itr->end(); ++kill_itr)
                      {
                        if (kill_itr->is_string())
                          kills.emplace(kill_itr->get<std::string>());
                      }
                    }
                    else if (itr->is_string())
                    {
                      kills.emplace(itr->get<std::string>());
                    }
                  }
                }
                if (kills.empty())
                {
                  reply(CreateJSONError("no action taken"));
                  return;
                }
                r->loop()->call([r, endpoint, kills, reply]() {
                  auto ep = r->hiddenServiceContext().GetEndpointByName(endpoint);
                  if (ep == nullptr)
                  {
                    reply(CreateJSONError("no endpoint with name " + endpoint));
                    return;
                  }
                  std::size_t removed = 0;
                  for (auto kill : kills)
                  {
                    removed += ep->RemoveAllConvoTagsFor(std::move(kill));
                  }
                  reply(CreateJSONResponse(
                      "removed " + std::to_string(removed) + " flow" + (removed == 1 ? "" : "s")));
                });
              });
            })
        .add_request_command(
            "exit",
            [&](sispopmq::Message& msg) {
              HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
                if (r->IsServiceNode())
                {
                  reply(CreateJSONError("not supported"));
                  return;
                }
                std::optional<service::Address> exit;
                std::optional<std::string> lnsExit;
                IPRange range;
                bool map = true;
                const auto exit_itr = obj.find("exit");
                if (exit_itr != obj.end())
                {
                  service::Address addr;
                  const auto exit_str = exit_itr->get<std::string>();
                  if (service::NameIsValid(exit_str) or exit_str == "null")
                  {
                    lnsExit = exit_str;
                  }
                  else if (not addr.FromString(exit_str))
                  {
                    reply(CreateJSONError("invalid exit address"));
                    return;
                  }
                  else
                  {
                    exit = addr;
                  }
                }

                const auto unmap_itr = obj.find("unmap");
                if (unmap_itr != obj.end() and unmap_itr->get<bool>())
                {
                  map = false;
                }
                const auto range_itr = obj.find("range");
                if (range_itr == obj.end() or range_itr->is_null())
                {
                  // platforms without ipv6 support will shit themselves
                  // here if we give them an exit mapping that is ipv6
                  if constexpr (platform::supports_ipv6)
                  {
                    range.FromString("::/0");
                  }
                  else
                  {
                    range.FromString("0.0.0.0/0");
                  }
                }
                else if (not range.FromString(range_itr->get<std::string>()))
                {
                  reply(CreateJSONError("invalid ip range"));
                  return;
                }
                if (not platform::supports_ipv6 and not range.IsV4())
                {
                  reply(CreateJSONError("ipv6 ranges not supported on this platform"));
                  return;
                }
                std::string token;
                const auto token_itr = obj.find("token");
                if (token_itr != obj.end() and not token_itr->is_null())
                {
                  token = token_itr->get<std::string>();
                }

                std::string endpoint = "default";
                const auto endpoint_itr = obj.find("endpoint");
                if (endpoint_itr != obj.end())
                {
                  endpoint = endpoint_itr->get<std::string>();
                }
                r->loop()->call([map, exit, lnsExit, range, token, endpoint, r, reply]() mutable {
                  auto ep = r->hiddenServiceContext().GetEndpointByName(endpoint);
                  if (ep == nullptr)
                  {
                    reply(CreateJSONError("no endpoint with name " + endpoint));
                    return;
                  }
                  if (map and (exit.has_value() or lnsExit.has_value()))
                  {
                    auto mapExit = [=](service::Address addr) mutable {
                      ep->MapExitRange(range, addr);

                      bool shouldSendAuth = false;
                      if (not token.empty())
                      {
                        shouldSendAuth = true;
                        ep->SetAuthInfoForEndpoint(*exit, service::AuthInfo{token});
                      }
                      auto onGoodResult = [r, reply](std::string reason) {
                        if (r->HasClientExit())
                          reply(CreateJSONResponse(reason));
                        else
                          reply(CreateJSONError("we dont have an exit?"));
                      };
                      auto onBadResult = [r, reply, ep, range](std::string reason) {
                        r->routePoker()->Down();
                        ep->UnmapExitRange(range);
                        reply(CreateJSONError(reason));
                      };
                      if (addr.IsZero())
                      {
                        onGoodResult("added null exit");
                        return;
                      }
                      ep->MarkAddressOutbound(addr);
                      ep->EnsurePathToService(
                          addr,
                          [onBadResult, onGoodResult, shouldSendAuth, addrStr = addr.ToString()](
                              auto, service::OutboundContext* ctx) {
                            if (ctx == nullptr)
                            {
                              onBadResult("could not find exit");
                              return;
                            }
                            if (not shouldSendAuth)
                            {
                              onGoodResult("OK: connected to " + addrStr);
                              return;
                            }
                            ctx->AsyncSendAuth(
                                [onGoodResult, onBadResult](service::AuthResult result) {
                                  // TODO: refactor this code.  We are 5 lambdas deep here!
                                  if (result.code != service::AuthResultCode::eAuthAccepted)
                                  {
                                    onBadResult(result.reason);
                                    return;
                                  }
                                  onGoodResult(result.reason);
                                });
                          });
                    };
                    if (exit.has_value())
                    {
                      mapExit(*exit);
                    }
                    else if (lnsExit.has_value())
                    {
                      const std::string name = *lnsExit;
                      if (name == "null")
                      {
                        service::Address nullAddr{};
                        mapExit(nullAddr);
                        return;
                      }
                      ep->LookupNameAsync(name, [reply, mapExit](auto maybe) mutable {
                        if (not maybe.has_value())
                        {
                          reply(CreateJSONError("we could not find an exit with that name"));
                          return;
                        }
                        if (auto ptr = std::get_if<service::Address>(&*maybe))
                        {
                          if (ptr->IsZero())
                            reply(CreateJSONError("name does not exist"));
                          else
                            mapExit(*ptr);
                        }
                        else
                        {
                          reply(CreateJSONError("lns name resolved to a snode"));
                        }
                      });
                    }
                    else
                    {
                      reply(
                          CreateJSONError("WTF inconsistent request, no exit address or lns "
                                          "name provided?"));
                    }
                    return;
                  }
                  else if (map and not exit.has_value())
                  {
                    reply(CreateJSONError("no exit address provided"));
                    return;
                  }
                  else if (not map)
                  {
                    r->routePoker()->Down();
                    ep->UnmapExitRange(range);
                    reply(CreateJSONResponse("OK"));
                  }
                });
              });
            })
        .add_request_command(
            "dns_query",
            [&](sispopmq::Message& msg) {
              HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
                std::string endpoint{"default"};
                if (const auto itr = obj.find("endpoint"); itr != obj.end())
                {
                  endpoint = itr->get<std::string>();
                }
                std::string qname{};
                dns::QType_t qtype = dns::qTypeA;
                if (const auto itr = obj.find("qname"); itr != obj.end())
                {
                  qname = itr->get<std::string>();
                }

                if (const auto itr = obj.find("qtype"); itr != obj.end())
                {
                  qtype = itr->get<dns::QType_t>();
                }

                dns::Message msg{dns::Question{qname, qtype}};

                if (auto ep_ptr = GetEndpointByName(r, endpoint))
                {
                  if (auto dns = ep_ptr->DNS())
                  {
                    auto src = std::make_shared<DummyPacketSource>([reply](auto result) {
                      if (result)
                        reply(CreateJSONResponse(result->ToJSON()));
                      else
                        reply(CreateJSONError("no response from dns"));
                    });
                    if (not dns->MaybeHandlePacket(src, src->dumb, src->dumb, msg.ToBuffer()))
                    {
                      reply(CreateJSONError("dns query not accepted by endpoint"));
                    }
                  }
                  else
                    reply(CreateJSONError("endpoint does not have dns"));
                  return;
                }
                reply(CreateJSONError("no such endpoint for dns query"));
              });
            })
        .add_request_command("config", [&](sispopmq::Message& msg) {
          HandleJSONRequest(msg, [r = m_Router](nlohmann::json obj, ReplyFunction_t reply) {
            {
              const auto itr = obj.find("override");
              if (itr != obj.end())
              {
                if (not itr->is_object())
                {
                  reply(CreateJSONError("override is not an object"));
                  return;
                }
                for (const auto& [section, value] : itr->items())
                {
                  if (not value.is_object())
                  {
                    reply(CreateJSONError(
                        fmt::format("failed to set [{}]: section is not an object", section)));
                    return;
                  }
                  for (const auto& [key, value] : value.items())
                  {
                    if (not value.is_string())
                    {
                      reply(CreateJSONError(fmt::format(
                          "failed to set [{}]:{}: value is not a string", section, key)));
                      return;
                    }
                    r->GetConfig()->Override(section, key, value.get<std::string>());
                  }
                }
              }
            }
            {
              const auto itr = obj.find("reload");
              if (itr != obj.end() and itr->get<bool>())
              {
                r->QueueDiskIO([conf = r->GetConfig()]() { conf->Save(); });
              }
            }
            reply(CreateJSONResponse("OK"));
          });
        });
  }

  void
  RpcServer::HandleLogsSubRequest(sispopmq::Message& m)
  {
    if (m.data.size() != 1)
    {
      m.send_reply("Invalid subscription request: no log receipt endpoint given");
      return;
    }

    auto endpoint = std::string{m.data[0]};

    if (endpoint == "unsubscribe")
    {
      log::info(logcat, "New logs unsubscribe request from conn {}@{}", m.conn, m.remote);
      log_subs.unsubscribe(m.conn);
      m.send_reply("OK");
      return;
    }

    auto is_new = log_subs.subscribe(m.conn, endpoint);

    if (is_new)
    {
      log::info(logcat, "New logs subscription request from conn {}@{}", m.conn, m.remote);
      m.send_reply("OK");
      log_subs.send_all(m.conn, endpoint);
    }
    else
    {
      log::debug(logcat, "Renewed logs subscription request from conn id {}@{}", m.conn, m.remote);
      m.send_reply("ALREADY");
    }
  }

}  // namespace llarp::rpc
