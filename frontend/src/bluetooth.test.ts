import { beforeEach, describe, expect, it, vi } from "vitest";

import { bluetoothPort, bluetoothPorts, hasBluetooth, isBluetoothPort, rememberBoard, rememberedBoards } from "@/bluetooth";

describe("Bluetooth ports", () => {
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

	it("are offered only for boards with Bluetooth", () => {
		expect(hasBluetooth("esp32")).toBe(true);
		expect(hasBluetooth("uno")).toBe(false);
		const boards = [{ name: "S2A-BBCC", address: "24:6F:28:AA:BB:CC" }];
		expect(bluetoothPorts("uno", boards, "Bluetooth - {name}")).toEqual([]);
		expect(bluetoothPorts("esp32", boards, "Bluetooth - {name}")).toEqual([
			{ address: "bt:24:6F:28:AA:BB:CC", label: "Bluetooth - S2A-BBCC" },
		]);
	});

	it("recognises Bluetooth ports, including a board paired on macOS", () => {
		expect(isBluetoothPort(bluetoothPort("24:6F:28:AA:BB:CC"))).toBe(true);
		expect(isBluetoothPort("/dev/cu.S2A-BBCC")).toBe(true);
		expect(isBluetoothPort("/dev/ttyUSB0")).toBe(false);
		expect(isBluetoothPort("wifi:192.168.4.1")).toBe(false);
		expect(isBluetoothPort(null)).toBe(false);
	});

	it("remembers the last boards found, most recent first, without duplicates", () => {
		expect(rememberedBoards()).toEqual([]);
		rememberBoard({ name: "S2A-0001", address: "00:00:00:00:00:01" });
		rememberBoard({ name: "S2A-0002", address: "00:00:00:00:00:02" });
		rememberBoard({ name: "S2A-0001", address: "00:00:00:00:00:01" });
		rememberBoard({ name: "S2A-0003", address: "00:00:00:00:00:03" });
		rememberBoard({ name: "S2A-0004", address: "00:00:00:00:00:04" });
		expect(rememberedBoards().map((board) => board.name)).toEqual(["S2A-0004", "S2A-0003", "S2A-0001"]);
	});

	it("ignores a damaged memory", () => {
		localStorage.setItem("s2a.bluetooth.boards", "{not json");
		expect(rememberedBoards()).toEqual([]);
		localStorage.setItem("s2a.bluetooth.boards", JSON.stringify([{ name: 1 }, { name: "S2A-1", address: "x" }]));
		expect(rememberedBoards()).toEqual([{ name: "S2A-1", address: "x" }]);
	});
});
