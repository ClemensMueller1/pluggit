"""Pluggit Avent P Home Assistant integration."""

from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.helpers import entity_registry as er

from .const import DOMAIN, FanStage
from .coordinator import PluggitCoordinator

PLATFORMS: list[Platform] = [
    Platform.FAN,
    Platform.SENSOR,
    Platform.BINARY_SENSOR,
]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up one Avent P unit from a config entry."""
    coordinator = PluggitCoordinator(hass, entry)
    await coordinator.async_setup()
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    async def _set_stage(call: ServiceCall) -> None:
        stage = FanStage(int(call.data["stage"]))
        entity_ids = call.data.get("entity_id")
        if isinstance(entity_ids, str):
            entity_ids = [entity_ids]
        registry = er.async_get(hass)
        for coordinator in hass.data[DOMAIN].values():
            if not isinstance(coordinator, PluggitCoordinator):
                continue
            if entity_ids:
                uid = f"{coordinator.entry.entry_id}_fan"
                entity = registry.async_get_entity_id("fan", DOMAIN, uid)
                if entity not in entity_ids:
                    continue
            await coordinator.async_set_stage(stage)

    if not hass.services.has_service(DOMAIN, "set_stage"):
        hass.services.async_register(DOMAIN, "set_stage", _set_stage)

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    coordinator: PluggitCoordinator = hass.data[DOMAIN].pop(entry.entry_id)
    await coordinator.async_unload()
    return unload_ok


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)



