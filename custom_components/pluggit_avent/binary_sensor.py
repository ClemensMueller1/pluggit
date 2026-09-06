"""Binary sensors for Pluggit Avent P."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import EntityCategory
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import (
    CONF_DEVICE_NAME,
    CONF_MODE,
    DEFAULT_NAME,
    DOMAIN,
    MODE_MQTT,
    MODE_RELAY,
)
from .coordinator import PluggitCoordinator, PluggitData


@dataclass(frozen=True, kw_only=True)
class PluggitBinaryDescription(BinarySensorEntityDescription):
    value_fn: Callable[[PluggitData], bool | None]
    modes: tuple[str, ...]


SENSORS: tuple[PluggitBinaryDescription, ...] = (
    PluggitBinaryDescription(
        key="filter_alarm",
        translation_key="filter_alarm",
        device_class=BinarySensorDeviceClass.PROBLEM,
        value_fn=lambda d: d.filter_alarm,
        modes=(MODE_MQTT,),
    ),
    PluggitBinaryDescription(
        key="fault",
        translation_key="fault",
        device_class=BinarySensorDeviceClass.PROBLEM,
        value_fn=lambda d: d.fault,
        modes=(MODE_MQTT,),
    ),
    PluggitBinaryDescription(
        key="bypass",
        translation_key="bypass",
        icon="mdi:valve",
        value_fn=lambda d: d.bypass,
        modes=(MODE_MQTT,),
    ),
    PluggitBinaryDescription(
        key="interlock_fault",
        translation_key="interlock_fault",
        device_class=BinarySensorDeviceClass.PROBLEM,
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.interlock_fault,
        modes=(MODE_RELAY,),
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: PluggitCoordinator = hass.data[DOMAIN][entry.entry_id]
    mode = entry.options.get(CONF_MODE, entry.data[CONF_MODE])
    async_add_entities(
        [
            PluggitBinarySensor(coordinator, entry, desc)
            for desc in SENSORS
            if mode in desc.modes
        ]
    )


class PluggitBinarySensor(CoordinatorEntity[PluggitCoordinator], BinarySensorEntity):
    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: PluggitCoordinator,
        entry: ConfigEntry,
        description: PluggitBinaryDescription,
    ) -> None:
        super().__init__(coordinator)
        self.entity_description = description
        name = entry.options.get(
            CONF_DEVICE_NAME, entry.data.get(CONF_DEVICE_NAME, DEFAULT_NAME)
        )
        self._attr_unique_id = f"{entry.entry_id}_{description.key}"
        self._attr_device_info = {
            "identifiers": {(DOMAIN, entry.entry_id)},
            "name": name,
            "manufacturer": "Pluggit",
            "model": "Avent P / P300 / P450",
        }

    @property
    def is_on(self) -> bool | None:
        return self.entity_description.value_fn(self.coordinator.data)
