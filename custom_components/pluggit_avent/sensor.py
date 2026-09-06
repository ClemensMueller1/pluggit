"""Sensors for Pluggit Avent P."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE, UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import (
    CONF_DEVICE_NAME,
    CONF_MODE,
    DEFAULT_NAME,
    DOMAIN,
    MODE_MQTT,
    STAGE_TO_PRESET,
)
from .coordinator import PluggitCoordinator, PluggitData


@dataclass(frozen=True, kw_only=True)
class PluggitSensorDescription(SensorEntityDescription):
    value_fn: Callable[[PluggitData], float | str | None]
    mqtt_only: bool = False


SENSORS: tuple[PluggitSensorDescription, ...] = (
    PluggitSensorDescription(
        key="stage",
        translation_key="stage",
        icon="mdi:fan",
        value_fn=lambda d: STAGE_TO_PRESET.get(d.stage),
    ),
    PluggitSensorDescription(
        key="t1",
        translation_key="t1",
        device_class=SensorDeviceClass.TEMPERATURE,
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=1,
        value_fn=lambda d: d.t1,
        mqtt_only=True,
    ),
    PluggitSensorDescription(
        key="t2",
        translation_key="t2",
        device_class=SensorDeviceClass.TEMPERATURE,
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=1,
        value_fn=lambda d: d.t2,
        mqtt_only=True,
    ),
    PluggitSensorDescription(
        key="t3",
        translation_key="t3",
        device_class=SensorDeviceClass.TEMPERATURE,
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=1,
        value_fn=lambda d: d.t3,
        mqtt_only=True,
    ),
    PluggitSensorDescription(
        key="t4",
        translation_key="t4",
        device_class=SensorDeviceClass.TEMPERATURE,
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=1,
        value_fn=lambda d: d.t4,
        mqtt_only=True,
    ),
    PluggitSensorDescription(
        key="humidity",
        translation_key="humidity",
        device_class=SensorDeviceClass.HUMIDITY,
        native_unit_of_measurement=PERCENTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=0,
        value_fn=lambda d: d.humidity,
        mqtt_only=True,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: PluggitCoordinator = hass.data[DOMAIN][entry.entry_id]
    mode = entry.options.get(CONF_MODE, entry.data[CONF_MODE])
    entities = [
        PluggitSensor(coordinator, entry, desc)
        for desc in SENSORS
        if not desc.mqtt_only or mode == MODE_MQTT
    ]
    async_add_entities(entities)


class PluggitSensor(CoordinatorEntity[PluggitCoordinator], SensorEntity):
    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: PluggitCoordinator,
        entry: ConfigEntry,
        description: PluggitSensorDescription,
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
    def native_value(self) -> float | str | None:
        return self.entity_description.value_fn(self.coordinator.data)

    @property
    def available(self) -> bool:
        if self.entity_description.mqtt_only:
            return (
                self.coordinator.data.available
                and self.entity_description.value_fn(self.coordinator.data) is not None
            )
        return self.coordinator.data.available
