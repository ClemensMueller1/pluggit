"""Fan entity for Pluggit Avent P."""

from __future__ import annotations

from typing import Any

from homeassistant.components.fan import FanEntity, FanEntityFeature
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import (
    CONF_DEVICE_NAME,
    CONF_MODE,
    DEFAULT_NAME,
    DOMAIN,
    FanStage,
    MODE_ANALOG,
    MODE_MQTT,
    MODE_RELAY,
    PRESET_MODES_ANALOG,
    PRESET_MODES_MQTT,
    PRESET_MODES_RELAY,
    PRESET_TO_STAGE,
    STAGE_TO_PERCENTAGE,
    STAGE_TO_PRESET,
    percentage_to_stage,
)
from .coordinator import PluggitCoordinator


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: PluggitCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([PluggitFan(coordinator, entry)])


class PluggitFan(CoordinatorEntity[PluggitCoordinator], FanEntity):
    """Discrete 3-speed fan mapped onto the Avent P stages."""

    _attr_has_entity_name = True
    _attr_name = None
    _attr_translation_key = "ventilation"
    _attr_speed_count = 3

    def __init__(self, coordinator: PluggitCoordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator)
        self._entry = entry
        name = entry.options.get(
            CONF_DEVICE_NAME, entry.data.get(CONF_DEVICE_NAME, DEFAULT_NAME)
        )
        self._attr_unique_id = f"{entry.entry_id}_fan"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, entry.entry_id)},
            "name": name,
            "manufacturer": "Pluggit",
            "model": "Avent P / P300 / P450",
        }
        mode = coordinator.mode
        if mode == MODE_ANALOG:
            presets = list(PRESET_MODES_ANALOG)
            features = (
                FanEntityFeature.SET_SPEED
                | FanEntityFeature.PRESET_MODE
                | FanEntityFeature.TURN_ON
                | FanEntityFeature.TURN_OFF
            )
        elif mode == MODE_MQTT:
            presets = list(PRESET_MODES_MQTT)
            features = (
                FanEntityFeature.SET_SPEED
                | FanEntityFeature.PRESET_MODE
                | FanEntityFeature.TURN_ON
                | FanEntityFeature.TURN_OFF
            )
        else:
            presets = list(PRESET_MODES_RELAY)
            features = (
                FanEntityFeature.SET_SPEED
                | FanEntityFeature.PRESET_MODE
                | FanEntityFeature.TURN_ON
            )
        self._attr_preset_modes = presets
        self._attr_supported_features = features

    @property
    def available(self) -> bool:
        return self.coordinator.data.available

    @property
    def is_on(self) -> bool:
        if self.coordinator.mode == MODE_ANALOG:
            return self.coordinator.data.stage != FanStage.REMOTE
        return True

    @property
    def percentage(self) -> int | None:
        return STAGE_TO_PERCENTAGE.get(self.coordinator.data.stage)

    @property
    def preset_mode(self) -> str | None:
        return STAGE_TO_PRESET.get(self.coordinator.data.stage)

    async def async_set_percentage(self, percentage: int) -> None:
        allow_remote = self.coordinator.mode == MODE_ANALOG
        stage = percentage_to_stage(percentage, allow_remote=allow_remote)
        if percentage == 0 and self.coordinator.mode == MODE_MQTT:
            stage = FanStage.STAGE1
        await self.coordinator.async_set_stage(stage)

    async def async_set_preset_mode(self, preset_mode: str) -> None:
        stage = PRESET_TO_STAGE.get(preset_mode, FanStage.STAGE2)
        if self.coordinator.mode == MODE_RELAY and stage == FanStage.REMOTE:
            stage = FanStage.STAGE2
        await self.coordinator.async_set_stage(stage)

    async def async_turn_on(
        self,
        percentage: int | None = None,
        preset_mode: str | None = None,
        **kwargs: Any,
    ) -> None:
        if preset_mode:
            await self.async_set_preset_mode(preset_mode)
        elif percentage is not None:
            await self.async_set_percentage(percentage)
        else:
            await self.coordinator.async_set_stage(FanStage.STAGE2)

    async def async_turn_off(self, **kwargs: Any) -> None:
        if self.coordinator.mode == MODE_ANALOG:
            await self.coordinator.async_set_stage(FanStage.REMOTE)
        else:
            await self.coordinator.async_set_stage(FanStage.STAGE1)
