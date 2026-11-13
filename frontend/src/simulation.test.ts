import { describe, expect, it } from "vitest";

import type { SimState, WiringPlan } from "@/bridge";
import { boardPins, isSimPort, keptBoardPins, percent, simParts, SIM_PORT, usesPhone } from "@/simulation";

const component = (kind: string, name: string, pins: Record<string, string>): WiringPlan["components"][number] => ({
	kind,
	name,
	connections: [
		...Object.entries(pins).map(([role, boardPin]) => ({ componentPin: role, boardPin, via: "", role })),
		{ componentPin: "GND", boardPin: "GND", via: "", role: "GND" },
	],
	note: "",
	blockIds: [],
});

const state = (changes: Partial<SimState>): SimState => ({
	modes: {},
	outputs: {},
	servos: {},
	tones: {},
	lcd: ["", ""],
	lcd16: ["", ""],
	oled: ["", "", "", "", "", "", "", ""],
	pixels: {},
	matrices: {},
	phone: [],
	pressed: {},
	analog: {},
	distances: {},
	climates: {},
	...changes,
});

describe("simulation", () => {
	it("recognises the virtual board's port", () => {
		expect(isSimPort(SIM_PORT)).toBe(true);
		expect(isSimPort("/dev/ttyACM0")).toBe(false);
		expect(isSimPort(null)).toBe(false);
	});

	it("draws each component of the plan with the state of its pins", () => {
		const plan: WiringPlan = {
			components: [
				component("output", "LED", { PIN: "9" }),
				component("button", "Button", { PIN: "2" }),
				component("potentiometer", "Pot", { PIN: "A0" }),
				component("ultrasonic", "Distance", { TRIG_PIN: "7", ECHO_PIN: "8" }),
				component("dht", "DHT", { PIN: "4" }),
				component("rgb_led", "RGB", { RED_PIN: "3", GREEN_PIN: "5", BLUE_PIN: "6" }),
				component("dc_motor", "Motor", { EN_PIN: "10", IN1_PIN: "11", IN2_PIN: "12" }),
				component("matrix", "Matrix", { DIN_PIN: "A1", CS_PIN: "A2", CLK_PIN: "A3" }),
				component("lcd_i2c", "LCD", {}),
				component("bluetooth_module", "HC-05", { RX_PIN: "10", TX_PIN: "11" }),
			],
			warnings: [],
		};
		const parts = simParts(
			plan,
			state({
				outputs: {
					"9": { level: 128, pwm: true },
					"3": { level: 255, pwm: false },
					"10": { level: 255, pwm: true },
					"12": { level: 255, pwm: false },
				},
				pressed: { "2": true },
				analog: { A0: 1023 },
				lcd: ["Hello", ""],
				matrices: { A1: [0x81, 0, 0, 0, 0, 0, 0, 0] },
			})
		);
		expect(parts.map((part) => part.kind)).toEqual([
			"led",
			"button",
			"slider",
			"distance",
			"climate",
			"rgb",
			"motor",
			"matrix",
			"screen",
		]); // the Bluetooth module is not drawn
		expect(parts[0]).toMatchObject({ pin: "9", level: 128, key: "output|9" });
		expect(parts[1]).toMatchObject({ pressed: true });
		expect(parts[2]).toMatchObject({ sensor: "potentiometer", value: 1023 });
		expect(parts[3]).toMatchObject({ pin: "7", centimetres: 100 }); // nobody moved it yet
		expect(parts[4]).toMatchObject({ temperature: 21, humidity: 50 });
		expect(parts[5]).toMatchObject({ color: [255, 0, 0] });
		expect(parts[6]).toMatchObject({ speed: -255 }); // IN2 high: backward
		expect(parts[7]).toMatchObject({ rows: [0x81, 0, 0, 0, 0, 0, 0, 0] });
		expect(parts[8]).toMatchObject({ screen: "lcd", lines: ["Hello", ""] });
		expect(simParts(null, null)).toEqual([]);
	});

	it("shows what the 4-digit display shows", () => {
		const plan: WiringPlan = { components: [component("tm1637", "Digits", { CLK_PIN: "4", DIO_PIN: "5" })], warnings: [] };
		expect(simParts(plan, state({ digits: { "4": "  42" } }))).toMatchObject([{ kind: "digits", pin: "4", text: "  42" }]);
		expect(simParts(plan, state({}))).toMatchObject([{ kind: "digits", text: "" }]);
	});

	it("shows the angle a stepper motor turned", () => {
		const plan: WiringPlan = {
			components: [component("stepper", "Stepper", { IN1_PIN: "8", IN2_PIN: "9", IN3_PIN: "10", IN4_PIN: "11" })],
			warnings: [],
		};
		expect(simParts(plan, state({ steppers: { "8": -90 } }))).toMatchObject([{ kind: "stepper", pin: "8", angle: -90 }]);
		expect(simParts(plan, state({}))).toMatchObject([{ kind: "stepper", angle: 0 }]); // older host: no steppers
	});

	it("gives a joystick two sliders and a button, and the PIR and obstacle sensors a button", () => {
		const plan: WiringPlan = {
			components: [
				component("joystick", "Joystick", { X_PIN: "A0", Y_PIN: "A1", SW_PIN: "2" }),
				component("pir", "PIR", { PIN: "3" }),
				component("obstacle_sensor", "IR", { PIN: "4" }),
			],
			warnings: [],
		};
		const parts = simParts(plan, state({ analog: { A1: 100 }, pressed: { "2": true, "4": true } }));
		expect(parts.map((part) => [part.kind, part.detail ?? ""])).toEqual([
			["slider", "sim.joystick_x"],
			["slider", "sim.joystick_y"],
			["button", "sim.joystick_press"],
			["button", ""],
			["button", ""],
		]);
		expect(new Set(parts.map((part) => part.key)).size).toBe(5); // React keys stay unique
		expect(parts[0]).toMatchObject({ pin: "A0", value: 512 });
		expect(parts[1]).toMatchObject({ pin: "A1", value: 100 });
		expect(parts[2]).toMatchObject({ pin: "2", pressed: true });
		expect(parts[3]).toMatchObject({ pin: "3", pressed: false, action: "move" });
		expect(parts[4]).toMatchObject({ pin: "4", pressed: true, action: "near" });
	});

	it("lists every pin the program uses, with what it does, in the board's order", () => {
		const pins = boardPins(
			state({
				modes: { A0: "input", "2": "pullup", "4": "input", "13": "output", "6": "output" },
				outputs: { "13": { level: 255, pwm: false }, "9": { level: 128, pwm: true }, "6": { level: 0, pwm: false } },
				tones: { "6": 440 },
				pressed: { "2": true },
				analog: { A0: 300 },
			})
		);
		expect(pins).toEqual([
			{ pin: "2", kind: "input", high: false, pullup: true }, // a button to GND, pressed
			{ pin: "4", kind: "input", high: false, pullup: false },
			{ pin: "6", kind: "tone", frequency: 440 },
			{ pin: "9", kind: "output", level: 128, pwm: true },
			{ pin: "13", kind: "output", level: 255, pwm: false },
			{ pin: "A0", kind: "analog", value: 300 },
		]);
		expect(boardPins(null)).toEqual([]);
	});

	it("keeps the pins used during the Live session in their place", () => {
		const seen = new Map();
		const sounding = keptBoardPins(boardPins(state({ modes: { "2": "pullup" }, tones: { "8": 440 } })), seen);
		expect(sounding.map((pin) => pin.pin)).toEqual(["2", "8"]);
		// The note ended: pin 8 is free again, it stays as a silent buzzer.
		const silent = keptBoardPins(boardPins(state({ modes: { "2": "pullup" } })), seen);
		expect(silent).toEqual([
			{ pin: "2", kind: "input", high: true, pullup: true },
			{ pin: "8", kind: "tone", frequency: 0 },
		]);
		// The next note sets pin 8 as an output for a while: still the buzzer.
		const again = keptBoardPins(boardPins(state({ modes: { "2": "pullup", "8": "output" }, outputs: { "8": { level: 0, pwm: false } } })), seen);
		expect(again[1]).toEqual({ pin: "8", kind: "tone", frequency: 0 });
		// An LED switched off keeps its place, off.
		keptBoardPins(boardPins(state({ outputs: { "13": { level: 255, pwm: false } } })), seen);
		expect(keptBoardPins(boardPins(state({})), seen).find((pin) => pin.pin === "13")).toEqual({ pin: "13", kind: "output", level: 0, pwm: false });
	});

	it("tells when the program talks to the phone", () => {
		expect(usesPhone('{"type":"phone_received"}')).toBe(true);
		expect(usesPhone('{"type":"pins_builtin_led"}')).toBe(false);
		expect(percent(1023)).toBe(100);
		expect(percent(0)).toBe(0);
	});
});
