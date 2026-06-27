# Copyright (c) Build or Bust.
# Python toolset bridge for the Unreal MCP server (Experimental ModelContextProtocol plugin).
#
# Epic's MCP plugin ships only a skill-management toolset and is otherwise an
# empty framework: to let an MCP client drive the editor you must register a
# toolset. This file defines one. Its star tool is `run_python`, which executes
# arbitrary Python inside the running editor -- enough to spawn actors, edit the
# level, create/save assets, configure navigation, etc. So once this is live we
# rarely need to touch this file again; everything else goes through run_python.

import io
import contextlib
import traceback

import unreal
import toolset_registry

# Namespace shared across run_python calls, so imports and variables persist
# between tool invocations (handy for building things up step by step).
_NS = {"unreal": unreal}


@unreal.uclass()
class BobMcpToolset(unreal.ToolsetDefinition):
    """Build or Bust editor-control tools (Python bridge)."""

    @toolset_registry.tool_call
    @staticmethod
    def run_python(code: str) -> str:
        """Execute arbitrary Python code inside the running Unreal Editor.

        The `unreal` module is in scope, as is anything defined by previous
        run_python calls (the namespace persists for the editor session). Use
        this to spawn actors, edit the level, create/save assets, set
        properties, build geometry, configure navigation, etc. Editor mutations
        are valid here because tools run on the game thread.

        IMPORTANT: exec() does not return the value of the last expression --
        use print(...) for any data you want returned. stdout and stderr are
        captured and returned; if an exception is raised its traceback is
        appended after an "--- EXCEPTION ---" marker.

        Args:
            code: Python source to execute in the editor interpreter.

        Returns:
            Captured stdout/stderr, plus a traceback if an exception occurred.
        """
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
                exec(code, _NS)
        except Exception:
            buf.write("\n--- EXCEPTION ---\n")
            buf.write(traceback.format_exc())
        out = buf.getvalue()
        return out if out.strip() else "(ok, no output)"

    @toolset_registry.tool_call
    @staticmethod
    def unreal_version() -> str:
        """Return the Unreal Engine version string. A quick end-to-end sanity check."""
        return unreal.SystemLibrary.get_engine_version()

    @toolset_registry.tool_call
    @staticmethod
    def list_level_actors() -> str:
        """List every actor in the current level as 'label | class | (x, y, z)'."""
        eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actors = eas.get_all_level_actors()
        lines = []
        for a in actors:
            loc = a.get_actor_location()
            lines.append(
                "{0} | {1} | ({2:.0f}, {3:.0f}, {4:.0f})".format(
                    a.get_actor_label(), a.get_class().get_name(), loc.x, loc.y, loc.z
                )
            )
        return "{0} actors in level:\n{1}".format(len(actors), "\n".join(lines))


def register() -> bool:
    """Register (or re-register) the toolset. Returns True if it is registered now,
    False if the toolset registry is not available yet (caller should retry later)."""
    from toolset_registry._registry_interface import get_toolset_registry

    registry = get_toolset_registry()
    if not registry.is_available():
        return False
    if registry.is_toolset_class_registered(BobMcpToolset):
        registry.unregister_toolset_class(BobMcpToolset)
    registry.register_toolset_class(BobMcpToolset)
    unreal.log("[BoB MCP] BobMcpToolset registered (run_python, unreal_version, list_level_actors).")
    return True


def unregister() -> None:
    """Remove the toolset from the registry."""
    from toolset_registry._registry_interface import get_toolset_registry

    registry = get_toolset_registry()
    if registry.is_available() and registry.is_toolset_class_registered(BobMcpToolset):
        registry.unregister_toolset_class(BobMcpToolset)
        unreal.log("[BoB MCP] BobMcpToolset unregistered.")
