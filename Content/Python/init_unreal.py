# Copyright (c) Build or Bust.
# Auto-run by Unreal Engine on editor startup for every Content/Python folder.
# Registers the Build or Bust MCP toolset bridge (see bob_mcp_tools.py).
#
# The toolset registry may not be ready this early in startup, so we try once
# immediately and, if it is not available yet, retry on slate ticks until it is
# (giving up after a few seconds rather than ticking forever).

import unreal

try:
    import bob_mcp_tools
except Exception as _import_err:  # pragma: no cover - defensive
    unreal.log_error("[BoB MCP] failed to import bob_mcp_tools: {0}".format(_import_err))
    bob_mcp_tools = None


def _try_register():
    """Return True when we should stop retrying (registered, or a hard failure)."""
    if bob_mcp_tools is None:
        return True
    try:
        return bob_mcp_tools.register()
    except Exception as err:
        unreal.log_error("[BoB MCP] register error: {0}".format(err))
        return True


if bob_mcp_tools is not None and not _try_register():
    _state = {"handle": None, "ticks": 0}

    def _on_tick(delta_seconds):
        _state["ticks"] += 1
        stop = _try_register()
        if stop or _state["ticks"] > 600:
            if _state["handle"] is not None:
                unreal.unregister_slate_post_tick_callback(_state["handle"])
            if not stop:
                unreal.log_error("[BoB MCP] gave up waiting for the toolset registry.")

    _state["handle"] = unreal.register_slate_post_tick_callback(_on_tick)
