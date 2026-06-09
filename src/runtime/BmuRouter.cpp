#include "BmuRouter.h"

namespace bmu_app
{

message_runtime::RouteDecision BmuRouter::route(
    const message_runtime::RouteContext &context)
{
    message_runtime::RouteDecision decision;

    // Injected (simulated) messages have no source session — send them
    // outbound. Inbound network messages (have a source session) should not be
    // forwarded back upstream to avoid loops.
    if (context.source_session_id.has_value())
    {
        // Message arrived from the network: drop (the runtime already delivered
        // it to the application via the traffic observer).
        decision.route_mode = message_runtime::RouteMode::drop;
    }
    else if (context.envelope.d_id.has_value())
    {
        // Outbound injection with a known destination node ID.
        decision.route_mode = message_runtime::RouteMode::route_by_destination;
    }
    else
    {
        // Outbound injection without a destination: broadcast.
        decision.route_mode = message_runtime::RouteMode::broadcast;
    }

    return decision;
}

} // namespace bmu_app
