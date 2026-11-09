import { beforeEach, describe, expect, it, vi } from "vitest";

import { hasWifi, isWifiPort, rememberAddress, rememberedAddresses, wifiPort, wifiPorts } from "@/wifi";

const labels = { accessPoint: "Wi-Fi — the board's network", home: "Wi-Fi — {address}" };

describe("Wi-Fi ports", () => {
	beforeEach(() => {
		// The test environment has no localStorage: an in-memory one.
		const items = new Map<string, string>();
		vi.stubGlobal("localStorage", {
			getItem: (key: string) => items.get(key) ?? null,
			setItem: (key: string, value: string) => void items.set(key, value),
			removeItem: (key: string) => void items.delete(key),
			clear: () => items.clear(),
		});
	});

	it("are offered only for boards with Wi-Fi", () => {
		expect(hasWifi("esp32")).toBe(true);
		expect(hasWifi("uno")).toBe(false);
		expect(wifiPorts("uno", ["192.168.1.42"], labels)).toEqual([]);
		expect(wifiPorts("esp32", ["192.168.1.42", "192.168.4.1"], labels)).toEqual([
			{ address: "wifi:192.168.4.1", label: "Wi-Fi — the board's network" },
			{ address: "wifi:192.168.1.42", label: "Wi-Fi — 192.168.1.42" },
		]);
	});

	it("are told apart from USB ports", () => {
		expect(isWifiPort(wifiPort("192.168.1.42"))).toBe(true);
		expect(isWifiPort("/dev/ttyUSB0")).toBe(false);
		expect(isWifiPort("COM3")).toBe(false);
		expect(isWifiPort(null)).toBe(false);
	});

	it("remember the last three home addresses, most recent first", () => {
		expect(rememberedAddresses()).toEqual([]);
		rememberAddress("10.0.0.1");
		rememberAddress("10.0.0.2");
		rememberAddress("10.0.0.3");
		expect(rememberAddress("10.0.0.1")).toEqual(["10.0.0.1", "10.0.0.3", "10.0.0.2"]);
		rememberAddress("10.0.0.4");
		expect(rememberedAddresses()).toEqual(["10.0.0.4", "10.0.0.1", "10.0.0.3"]);
		localStorage.setItem("s2a.wifi.addresses", "not json");
		expect(rememberedAddresses()).toEqual([]);
	});
});
