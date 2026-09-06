"""Data coordinator for Pluggit Avent P."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from datetime import timedelta
from typing import Any

from homeassistant.components.mqtt import async_publish, async_subscribe
from homeassistant.const import STATE_ON, STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.core import HomeAssistant, State, callback
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator

from .const import (
    CONF_ANALOG_ENTITY,
    CONF_MODE,
    CONF_MQTT_PREFIX,
    CONF_SWITCH_STAGE1,
    CONF_SWITCH_STAGE3,
    DOMAIN,
    FanStage,
    INTERLOCK_DELAY_S,
    MODE_ANALOG,
    MODE_MQTT,
    MODE_RELAY,
    MQTT_AVAILABILITY,
    MQTT_CMD_SPEED,
    MQTT_STATE_BYPASS,
    MQTT_STATE_FAULT,
    MQTT_STATE_FILTER,
    MQTT_STATE_RH,
    MQTT_STATE_SPEED,
    MQTT_STATE_T1,
    MQTT_STATE_T2,
    MQTT_STATE_T3,
    MQTT_STATE_T4,
    STAGE_TO_PERCENTAGE,
    STAGE_TO_VOLTAGE,
    voltage_to_stage,
)

_LOGGER = logging.getLogger(__name__)


def _is_on(state: State | None) -> bool:
    return state is not None and state.state == STATE_ON


def _float_or_none(value: Any) -> float | None:
    if value in (None, "", STATE_UNKNOWN, STATE_UNAVAILABLE):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


class PluggitData:
    """Snapshot of the ventilation unit."""

    def __init__(self) -> None:
        self.stage: FanStage = FanStage.STAGE2
        self.available: bool = True
        self.t1: float | None = None
        self.t2: float | None = None
        self.t3: float | None = None
        self.t4: float | None = None
        self.humidity: float | None = None
        self.filter_alarm: bool | None = None
        self.fault: bool | None = None
        self.bypass: bool | None = None
        self.interlock_fault: bool = False

    def as_dict(self) -> dict[str, Any]:
        return {
            "stage": int(self.stage),
            "available": self.available,
            "t1": self.t1,
            "t2": self.t2,
            "t3": self.t3,
            "t4": self.t4,
            "humidity": self.humidity,
            "filter_alarm": self.filter_alarm,
            "fault": self.fault,
            "bypass": self.bypass,
            "interlock_fault": self.interlock_fault,
        }


class PluggitCoordinator(DataUpdateCoordinator[PluggitData]):
    """Reads source entities / MQTT and writes fan stages."""

    def __init__(self, hass: HomeAssistant, entry) -> None:
        super().__init__(
            hass,
            _LOGGER,
            name=entry.title,
            update_interval=timedelta(seconds=30),
        )
        self.entry = entry
        self.data = PluggitData()
        self._unsubs: list[Callable[[], None]] = []
        self._lock = asyncio.Lock()

    @property
    def mode(self) -> str:
        return self.entry.options.get(CONF_MODE, self.entry.data[CONF_MODE])

    @property
    def cfg(self) -> dict[str, Any]:
        return {**self.entry.data, **self.entry.options}

    async def async_setup(self) -> None:
        """Subscribe to source state changes."""
        if self.mode == MODE_RELAY:
            entities = [self.cfg[CONF_SWITCH_STAGE1], self.cfg[CONF_SWITCH_STAGE3]]
            self._unsubs.append(
                async_track_state_change_event(
                    self.hass, entities, self._async_source_changed
                )
            )
        elif self.mode == MODE_ANALOG:
            self._unsubs.append(
                async_track_state_change_event(
                    self.hass,
                    [self.cfg[CONF_ANALOG_ENTITY]],
                    self._async_source_changed,
                )
            )
        else:
            await self._async_subscribe_mqtt()
        await self.async_refresh()

    async def async_unload(self) -> None:
        for unsub in self._unsubs:
            unsub()
        self._unsubs.clear()

    @callback
    def _async_source_changed(self, _event) -> None:
        self.hass.async_create_task(self.async_request_refresh())

    async def _async_subscribe_mqtt(self) -> None:
        prefix = self.cfg[CONF_MQTT_PREFIX]

        async def _msg(key: str, convert):
            @callback
            def _handler(msg) -> None:
                payload = msg.payload
                if isinstance(payload, bytes):
                    payload = payload.decode()
                setattr(self.data, key, convert(payload))
                self.async_set_updated_data(self.data)

            return _handler

        def _stage(payload: str) -> FanStage:
            try:
                value = int(float(payload))
            except (TypeError, ValueError):
                return self.data.stage
            try:
                return FanStage(value)
            except ValueError:
                return self.data.stage

        def _bool(payload: str) -> bool:
            return payload.lower() in ("1", "true", "on", "yes")

        mapping = {
            MQTT_STATE_SPEED: ("stage", _stage),
            MQTT_STATE_T1: ("t1", _float_or_none),
            MQTT_STATE_T2: ("t2", _float_or_none),
            MQTT_STATE_T3: ("t3", _float_or_none),
            MQTT_STATE_T4: ("t4", _float_or_none),
            MQTT_STATE_RH: ("humidity", _float_or_none),
            MQTT_STATE_FILTER: ("filter_alarm", _bool),
            MQTT_STATE_FAULT: ("fault", _bool),
            MQTT_STATE_BYPASS: ("bypass", _bool),
        }
        for topic_suffix, (attr, conv) in mapping.items():
            handler = await _msg(attr, conv)
            self._unsubs.append(
                await async_subscribe(self.hass, f"{prefix}/{topic_suffix}", handler)
            )

        @callback
        def _avail(msg) -> None:
            payload = msg.payload
            if isinstance(payload, bytes):
                payload = payload.decode()
            self.data.available = payload.lower() in ("online", "1", "true")
            self.async_set_updated_data(self.data)

        self._unsubs.append(
            await async_subscribe(self.hass, f"{prefix}/{MQTT_AVAILABILITY}", _avail)
        )

    async def _async_update_data(self) -> PluggitData:
        if self.mode == MODE_RELAY:
            s1 = self.hass.states.get(self.cfg[CONF_SWITCH_STAGE1])
            s3 = self.hass.states.get(self.cfg[CONF_SWITCH_STAGE3])
            on1, on3 = _is_on(s1), _is_on(s3)
            self.data.interlock_fault = on1 and on3
            if on1 and on3:
                _LOGGER.warning(
                    "Both stage relays are on; the Avent P contact input is in an invalid state"
                )
                self.data.stage = FanStage.STAGE2
            elif on1:
                self.data.stage = FanStage.STAGE1
            elif on3:
                self.data.stage = FanStage.STAGE3
            else:
                self.data.stage = FanStage.STAGE2
            unavailable = {STATE_UNAVAILABLE, STATE_UNKNOWN, None}
            self.data.available = (
                s1 is not None
                and s3 is not None
                and s1.state not in unavailable
                and s3.state not in unavailable
            )
        elif self.mode == MODE_ANALOG:
            state = self.hass.states.get(self.cfg[CONF_ANALOG_ENTITY])
            voltage = _float_or_none(state.state if state else None)
            self.data.stage = voltage_to_stage(voltage)
            self.data.available = state is not None and state.state not in (
                STATE_UNAVAILABLE,
                STATE_UNKNOWN,
            )
        return self.data

    async def async_set_stage(self, stage: FanStage) -> None:
        """Apply a fan stage to the wired or MQTT backend."""
        async with self._lock:
            if self.mode == MODE_RELAY:
                await self._async_set_relays(stage)
            elif self.mode == MODE_ANALOG:
                await self._async_set_analog(stage)
            else:
                await self._async_set_mqtt(stage)
        self.data.stage = stage
        self.async_set_updated_data(self.data)

    async def _async_set_relays(self, stage: FanStage) -> None:
        if stage == FanStage.REMOTE:
            stage = FanStage.STAGE2
        s1 = self.cfg[CONF_SWITCH_STAGE1]
        s3 = self.cfg[CONF_SWITCH_STAGE3]
        # Always drop both first so J8 never sees both contacts closed.
        await self.hass.services.async_call(
            "switch", "turn_off", {"entity_id": [s1, s3]}, blocking=True
        )
        await asyncio.sleep(INTERLOCK_DELAY_S)
        if stage == FanStage.STAGE1:
            await self.hass.services.async_call(
                "switch", "turn_on", {"entity_id": s1}, blocking=True
            )
        elif stage == FanStage.STAGE3:
            await self.hass.services.async_call(
                "switch", "turn_on", {"entity_id": s3}, blocking=True
            )

    async def _async_set_analog(self, stage: FanStage) -> None:
        entity_id = self.cfg[CONF_ANALOG_ENTITY]
        voltage = STAGE_TO_VOLTAGE[stage]
        domain = entity_id.split(".", 1)[0]
        await self.hass.services.async_call(
            domain,
            "set_value",
            {"entity_id": entity_id, "value": voltage},
            blocking=True,
        )

    async def _async_set_mqtt(self, stage: FanStage) -> None:
        prefix = self.cfg[CONF_MQTT_PREFIX]
        await async_publish(
            self.hass,
            f"{prefix}/{MQTT_CMD_SPEED}",
            str(int(stage)),
            qos=1,
            retain=False,
        )

    def percentage(self) -> int:
        return STAGE_TO_PERCENTAGE.get(self.data.stage, 66)
