#pragma once

#include <routing_interfaces.h>

namespace bmu_app
{

// Routes decoded BMU messages:
//   - If the envelope has a d_id, route by destination (node ID lookup).
//   - Otherwise broadcast to all connected sessions.
// Injected simulation messages (no source session) are sent outbound;
// inbound network messages are never reflected back.
class BmuRouter : public message_runtime::IMessageRouter
{
  public:
    message_runtime::RouteDecision route(
        const message_runtime::RouteContext &context) override;
};

} // namespace bmu_app
