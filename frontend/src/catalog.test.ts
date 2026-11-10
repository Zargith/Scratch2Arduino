import { describe, expect, it } from "vitest";

import { findBoard } from "@/catalog";

describe("boards", () => {
	it("lists the analog pins of an Arduino board after its digital pins", () => {
		const uno = findBoard("uno");
		expect(uno?.pins.map((pin) => pin.label)).toEqual([
			...Array.from({ length: 14 }, (_, i) => String(i)),
			"A0",
			"A1",
			"A2",
			"A3",
			"A4",
			"A5",
		]);
	});

	it("lists the GPIO pins of an ESP32 by number, like the C++ core", () => {
		const esp32 = findBoard("esp32");
		expect(esp32?.pins.map((pin) => pin.label)).toEqual(
			["2", "4", "5", "13", "14", "15", "16", "17", "18", "19", "21", "22", "23", "25", "26", "27", "32", "33", "34", "35", "36", "39"]
		);
		expect(esp32?.pins.find((pin) => pin.label === "34")?.capabilities).toEqual(["analog"]);
		expect(esp32?.pins.find((pin) => pin.label === "32")?.capabilities).toEqual(["digital", "analog", "pwm", "interrupt"]);
	});
});
