"""Tool-calling conversation loop that drives eval interactions."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any

from .providers import AssistantMessage, Provider, ToolCall
from .sessions import DualMCPSessions


@dataclass
class ToolCallRecord:
    """Record of a single tool call and its result."""
    name: str
    arguments: dict[str, Any]
    result: str
    timestamp: float


@dataclass
class ConversationTrace:
    """Full trace of a multi-turn eval conversation."""
    messages: list[dict[str, Any]] = field(default_factory=list)
    tool_call_records: list[ToolCallRecord] = field(default_factory=list)
    final_answer: str | None = None
    truncated: bool = False
    turns: int = 0


def _format_tool_results_openai(
    assistant_msg: AssistantMessage,
    tool_results: list[tuple[ToolCall, str]],
) -> list[dict[str, Any]]:
    """Build OpenAI-format messages for an assistant response with tool calls and their results."""
    msgs: list[dict[str, Any]] = []

    # The assistant message with tool calls
    assistant_dict: dict[str, Any] = {"role": "assistant", "content": assistant_msg.text or ""}
    if assistant_msg.tool_calls:
        assistant_dict["tool_calls"] = [
            {
                "id": tc.id,
                "type": "function",
                "function": {"name": tc.name, "arguments": __import__("json").dumps(tc.arguments)},
            }
            for tc in assistant_msg.tool_calls
        ]
    msgs.append(assistant_dict)

    # Tool result messages
    for tc, result_text in tool_results:
        msgs.append({"role": "tool", "tool_call_id": tc.id, "content": result_text})

    return msgs


def _format_tool_results_anthropic(
    assistant_msg: AssistantMessage,
    tool_results: list[tuple[ToolCall, str]],
) -> list[dict[str, Any]]:
    """Build Anthropic-format messages for an assistant response with tool calls and their results."""
    msgs: list[dict[str, Any]] = []

    # Assistant message with content blocks
    content_blocks: list[dict[str, Any]] = []
    if assistant_msg.text:
        content_blocks.append({"type": "text", "text": assistant_msg.text})
    for tc in assistant_msg.tool_calls:
        content_blocks.append({
            "type": "tool_use",
            "id": tc.id,
            "name": tc.name,
            "input": tc.arguments,
        })
    msgs.append({"role": "assistant", "content": content_blocks})

    # Tool results in a single user message
    result_blocks: list[dict[str, Any]] = []
    for tc, result_text in tool_results:
        result_blocks.append({
            "type": "tool_result",
            "tool_use_id": tc.id,
            "content": result_text,
        })
    msgs.append({"role": "user", "content": result_blocks})

    return msgs


async def run_eval_loop(
    provider: Provider,
    sessions: DualMCPSessions,
    system_prompt: str,
    user_prompt: str,
    tools: list[dict[str, Any]],
    max_turns: int = 20,
) -> ConversationTrace:
    """Drive a multi-turn tool-calling conversation and return the full trace."""

    is_anthropic = provider.name == "anthropic"

    # Build initial messages
    messages: list[dict[str, Any]] = [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": user_prompt},
    ]

    trace = ConversationTrace()
    trace.messages = list(messages)

    for turn in range(max_turns):
        trace.turns = turn + 1
        response = await provider.complete(messages, tools)

        if not response.tool_calls:
            # Final answer
            trace.final_answer = response.text
            trace.messages.append({"role": "assistant", "content": response.text or ""})
            return trace

        # Execute tool calls
        tool_results: list[tuple[ToolCall, str]] = []
        for tc in response.tool_calls:
            ts = time.time()
            try:
                result_text = await sessions.call_tool(tc.name, tc.arguments)
            except Exception as e:
                result_text = f"Error: {e}"
            tool_results.append((tc, result_text))
            trace.tool_call_records.append(ToolCallRecord(
                name=tc.name,
                arguments=tc.arguments,
                result=result_text,
                timestamp=ts,
            ))

        # Format and append to message history
        if is_anthropic:
            new_msgs = _format_tool_results_anthropic(response, tool_results)
        else:
            new_msgs = _format_tool_results_openai(response, tool_results)
        messages.extend(new_msgs)
        trace.messages.extend(new_msgs)

    # Max turns reached — force a final answer by calling with no tools
    trace.truncated = True
    messages.append({
        "role": "user",
        "content": "You have reached the maximum number of tool calls. Please provide "
                   "your final answer now based on what you have learned so far.",
    })
    trace.messages.append(messages[-1])
    try:
        final = await provider.complete(messages, [])
        trace.final_answer = final.text
        trace.messages.append({"role": "assistant", "content": final.text or ""})
    except Exception:
        trace.final_answer = response.text if response else None
    trace.turns += 1
    return trace
