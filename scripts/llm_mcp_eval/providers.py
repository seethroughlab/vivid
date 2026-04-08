"""Provider adapters for OpenAI, Anthropic, and a fake (canned) provider."""

from __future__ import annotations

import json
import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any


@dataclass
class ToolCall:
    id: str
    name: str
    arguments: dict[str, Any]


@dataclass
class AssistantMessage:
    text: str | None
    tool_calls: list[ToolCall]
    stop_reason: str  # "end_turn" or "tool_use"


class Provider(ABC):
    """Abstract provider interface for LLM completions with tool calling."""

    name: str
    model: str

    @abstractmethod
    async def complete(
        self,
        messages: list[dict[str, Any]],
        tools: list[dict[str, Any]],
    ) -> AssistantMessage:
        ...


# ---------------------------------------------------------------------------
# Fake provider
# ---------------------------------------------------------------------------


class FakeProvider(Provider):
    """Returns canned responses from a scripted sequence. No API keys needed."""

    name = "fake"

    def __init__(self, sequence: list[AssistantMessage]) -> None:
        self.model = "fake"
        self._sequence = list(sequence)
        self._index = 0

    async def complete(
        self,
        messages: list[dict[str, Any]],
        tools: list[dict[str, Any]],
    ) -> AssistantMessage:
        if self._index >= len(self._sequence):
            return AssistantMessage(
                text="[fake provider: sequence exhausted]",
                tool_calls=[],
                stop_reason="end_turn",
            )
        msg = self._sequence[self._index]
        self._index += 1
        return msg


# ---------------------------------------------------------------------------
# OpenAI provider
# ---------------------------------------------------------------------------


class OpenAIProvider(Provider):
    """OpenAI chat completions with function calling."""

    name = "openai"

    def __init__(self, model: str = "gpt-4o", api_key: str | None = None) -> None:
        self.model = model
        self._api_key = api_key

    async def complete(
        self,
        messages: list[dict[str, Any]],
        tools: list[dict[str, Any]],
    ) -> AssistantMessage:
        from openai import AsyncOpenAI

        client = AsyncOpenAI(api_key=self._api_key)
        kwargs: dict[str, Any] = {
            "model": self.model,
            "messages": messages,
        }
        if tools:
            kwargs["tools"] = tools
        response = await client.chat.completions.create(**kwargs)
        choice = response.choices[0]
        msg = choice.message

        text = msg.content
        tool_calls: list[ToolCall] = []
        if msg.tool_calls:
            for tc in msg.tool_calls:
                tool_calls.append(ToolCall(
                    id=tc.id,
                    name=tc.function.name,
                    arguments=json.loads(tc.function.arguments),
                ))

        stop_reason = "tool_use" if tool_calls else "end_turn"
        return AssistantMessage(text=text, tool_calls=tool_calls, stop_reason=stop_reason)


# ---------------------------------------------------------------------------
# Anthropic provider
# ---------------------------------------------------------------------------


class AnthropicProvider(Provider):
    """Anthropic messages API with tool use."""

    name = "anthropic"

    def __init__(self, model: str = "claude-sonnet-4-20250514", api_key: str | None = None) -> None:
        self.model = model
        self._api_key = api_key

    async def complete(
        self,
        messages: list[dict[str, Any]],
        tools: list[dict[str, Any]],
    ) -> AssistantMessage:
        from anthropic import AsyncAnthropic

        client = AsyncAnthropic(api_key=self._api_key)

        # Extract system message from the list
        system_text = None
        api_messages: list[dict[str, Any]] = []
        for m in messages:
            if m["role"] == "system":
                system_text = m["content"]
            else:
                api_messages.append(m)

        kwargs: dict[str, Any] = {
            "model": self.model,
            "max_tokens": 8192,
            "messages": api_messages,
        }
        if system_text:
            kwargs["system"] = system_text
        if tools:
            kwargs["tools"] = tools

        response = await client.messages.create(**kwargs)

        text_parts: list[str] = []
        tool_calls: list[ToolCall] = []
        for block in response.content:
            if block.type == "text":
                text_parts.append(block.text)
            elif block.type == "tool_use":
                tool_calls.append(ToolCall(
                    id=block.id,
                    name=block.name,
                    arguments=block.input,
                ))

        text = "\n".join(text_parts) if text_parts else None
        stop_reason = "tool_use" if response.stop_reason == "tool_use" else "end_turn"
        return AssistantMessage(text=text, tool_calls=tool_calls, stop_reason=stop_reason)
