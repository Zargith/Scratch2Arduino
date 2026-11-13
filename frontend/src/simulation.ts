// Simulation: the virtual board that runs the blocks in Live mode without any hardware. The core
// answers like the Live firmware (core/include/s2a/simulator.hpp); this module turns the
// components of the project (its wiring plan) and the state of the virtual board into the parts
// that the Simulator panel draws.
import type { SimState, WiringPlan } from "@/bridge";
import type { MessageKey } from "@/i18n";

/** The "port" of the virtual board: the core recognises the "sim:" prefix. */
export const SIM_PORT = "sim:board";
export const isSimPort = (port: string | null): boolean => port?.startsWith("sim:") ?? false;

/** Same values as the core (sim::kDefaultAnalog...) for inputs nobody moved yet. */
export const DEFAULT_ANALOG = 512;
export const DEFAULT_DISTANCE = 100;
export const DEFAULT_TEMPERATURE = 21;
export const DEFAULT_HUMIDITY = 50;

interface Base {
	/** Stable key: the kind and the pins. */
	key: string;
	/** Name of the component, in the user's language. */
	name: string;
	/** Which part of the component, when it has several (the axes of a joystick). */
	detail?: MessageKey;
}

export type SimPart = Base &
	(
		| { kind: "led"; pin: string; level: number }
		| { kind: "rgb"; color: [number, number, number] }
		/** action: what the button stands for (pressing it, moving in front of a PIR, a hand near a sensor). */
		| { kind: "button"; pin: string; pressed: boolean; action?: "press" | "move" | "near" }
		| { kind: "slider"; sensor: "potentiometer" | "light" | "analog"; pin: string; value: number }
		| { kind: "distance"; pin: string; centimetres: number }
		| { kind: "climate"; pin: string; temperature: number; humidity: number }
		| { kind: "buzzer"; pin: string; frequency: number }
		| { kind: "servo"; pin: string; angle: number }
		/** angle: turned since Live started (degrees, may be negative or above 360). */
		| { kind: "stepper"; pin: string; angle: number }
		/** text: the 4 characters shown ("" when off). */
		| { kind: "digits"; pin: string; text: string }
		/** speed: -255 (backward) to 255 (forward). */
		| { kind: "motor"; speed: number }
		| { kind: "strip"; pin: string; colors: number[][] }
		| { kind: "matrix"; pin: string; rows: number[] }
		| { kind: "screen"; screen: "lcd" | "oled"; lines: string[] }
	);

const EMPTY: SimState = {
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
};

const POWER = /^(GND|5V|3V3|VCC|VIN|SDA|SCL)$/;

/** The parts to draw, in the order of the wiring plan (components without a drawing are left out). */
export function simParts(plan: WiringPlan | null, state: SimState | null): SimPart[] {
	const now = state ?? EMPTY;
	const level = (pin: string) => now.outputs[pin]?.level ?? 0;
	const parts: SimPart[] = [];
	for (const component of plan?.components ?? []) {
		const pin = (role: string) => component.connections.find((connection) => connection.role === role)?.boardPin ?? "";
		const signal = pin("PIN");
		const pins = component.connections
			.filter((connection) => connection.role && !POWER.test(connection.role))
			.map((connection) => connection.boardPin);
		const base = { key: [component.kind, ...pins].join("|"), name: component.name };
		switch (component.kind) {
			case "output":
				parts.push({ ...base, kind: "led", pin: signal, level: level(signal) });
				break;
			case "rgb_led":
				parts.push({ ...base, kind: "rgb", color: [level(pin("RED_PIN")), level(pin("GREEN_PIN")), level(pin("BLUE_PIN"))] });
				break;
			case "button":
			case "input":
				parts.push({ ...base, kind: "button", pin: signal, pressed: now.pressed[signal] ?? false });
				break;
			case "joystick":
				for (const [role, detail] of [
					["X_PIN", "sim.joystick_x"],
					["Y_PIN", "sim.joystick_y"],
				] as const) {
					const axis = pin(role);
					parts.push({ ...base, key: `${base.key}|${role}`, detail, kind: "slider", sensor: "potentiometer", pin: axis, value: now.analog[axis] ?? DEFAULT_ANALOG });
				}
				parts.push({ ...base, key: `${base.key}|SW_PIN`, detail: "sim.joystick_press", kind: "button", pin: pin("SW_PIN"), pressed: now.pressed[pin("SW_PIN")] ?? false });
				break;
			case "pir":
				parts.push({ ...base, kind: "button", pin: signal, pressed: now.pressed[signal] ?? false, action: "move" });
				break;
			case "obstacle_sensor":
				parts.push({ ...base, kind: "button", pin: signal, pressed: now.pressed[signal] ?? false, action: "near" });
				break;
			case "potentiometer":
			case "light_sensor":
			case "analog_input":
				parts.push({
					...base,
					kind: "slider",
					sensor: component.kind === "potentiometer" ? "potentiometer" : component.kind === "light_sensor" ? "light" : "analog",
					pin: signal,
					value: now.analog[signal] ?? DEFAULT_ANALOG,
				});
				break;
			case "ultrasonic": {
				const trig = pin("TRIG_PIN");
				parts.push({ ...base, kind: "distance", pin: trig, centimetres: now.distances[trig] ?? DEFAULT_DISTANCE });
				break;
			}
			case "dht": {
				const [temperature, humidity] = now.climates[signal] ?? [DEFAULT_TEMPERATURE, DEFAULT_HUMIDITY];
				parts.push({ ...base, kind: "climate", pin: signal, temperature, humidity });
				break;
			}
			case "buzzer":
				parts.push({ ...base, kind: "buzzer", pin: signal, frequency: now.tones[signal] ?? 0 });
				break;
			case "servo":
				parts.push({ ...base, kind: "servo", pin: signal, angle: now.servos[signal] ?? 90 });
				break;
			case "tm1637": {
				const clk = pin("CLK_PIN");
				parts.push({ ...base, kind: "digits", pin: clk, text: now.digits?.[clk] ?? "" });
				break;
			}
			case "stepper": {
				const in1 = pin("IN1_PIN");
				parts.push({ ...base, kind: "stepper", pin: in1, angle: now.steppers?.[in1] ?? 0 });
				break;
			}
			case "dc_motor": {
				const forward = level(pin("IN1_PIN")) > 0;
				const backward = level(pin("IN2_PIN")) > 0;
				const direction = forward === backward ? 0 : forward ? 1 : -1;
				parts.push({ ...base, kind: "motor", speed: direction * level(pin("EN_PIN")) });
				break;
			}
			case "neopixel":
				parts.push({ ...base, kind: "strip", pin: signal, colors: now.pixels[signal] ?? [] });
				break;
			case "matrix": {
				const din = pin("DIN_PIN");
				parts.push({ ...base, kind: "matrix", pin: din, rows: now.matrices[din] ?? [0, 0, 0, 0, 0, 0, 0, 0] });
				break;
			}
			case "lcd_i2c":
				parts.push({ ...base, kind: "screen", screen: "lcd", lines: now.lcd });
				break;
			case "lcd16":
				parts.push({ ...base, kind: "screen", screen: "lcd", lines: now.lcd16 });
				break;
			case "oled":
				parts.push({ ...base, kind: "screen", screen: "oled", lines: now.oled });
				break;
			default:
				break; // Bluetooth module: the phone has its own card
		}
	}
	return parts;
}

/** A pin the program uses, with what it does now (the "board pins" of the virtual board). */
export type BoardPin =
	| { pin: string; kind: "output"; level: number; pwm: boolean }
	/** A buzzer sounding on the pin. */
	| { pin: string; kind: "tone"; frequency: number }
	/** A digital input: high or low (a button to GND with the pull-up reads low while pressed). */
	| { pin: string; kind: "input"; high: boolean; pullup: boolean }
	/** An analog input (A0, A1...): 0 to 1023. */
	| { pin: string; kind: "analog"; value: number };

const pinOrder = (pin: string) => (/^\d+$/.test(pin) ? Number(pin) : 1000 + pin.charCodeAt(0) * 100 + (Number(pin.slice(1)) || 0));

/** Every pin the program uses (outputs, sounds, inputs), in the board's order ("2" before "10", digital before "A0"). */
export function boardPins(state: SimState | null): BoardPin[] {
	const now = state ?? EMPTY;
	const pins = new Map<string, BoardPin>();
	for (const [pin, mode] of Object.entries(now.modes)) {
		if (mode === "output") continue; // with its level below
		const pressed = now.pressed[pin] ?? false;
		const analog = /^A\d+$/.test(pin);
		pins.set(
			pin,
			analog && mode === "input"
				? { pin, kind: "analog", value: now.analog[pin] ?? DEFAULT_ANALOG }
				: { pin, kind: "input", high: mode === "pullup" ? !pressed : pressed, pullup: mode === "pullup" }
		);
	}
	for (const [pin, output] of Object.entries(now.outputs)) pins.set(pin, { pin, kind: "output", level: output.level, pwm: output.pwm });
	for (const [pin, frequency] of Object.entries(now.tones)) if (frequency > 0) pins.set(pin, { pin, kind: "tone", frequency });
	return [...pins.values()].sort((a, b) => pinOrder(a.pin) - pinOrder(b.pin));
}

/**
 * The board pins with those used earlier in this Live session kept in their place: a buzzer that
 * stops sounding (its pin is free again) shows "silent" instead of leaving the list, so the pins do
 * not come and go at each note. `seen` keeps what was seen (one map per Live session).
 */
export function keptBoardPins(now: BoardPin[], seen: Map<string, BoardPin>): BoardPin[] {
	const shown = new Map(now.map((pin) => [pin.pin, pin] as const));
	for (const [pin, before] of seen) {
		if (shown.has(pin)) continue;
		// No longer driven: an output or a sound is off now; an input keeps its last state.
		shown.set(pin, before.kind === "tone" ? { ...before, frequency: 0 } : before.kind === "output" ? { ...before, level: 0 } : before);
	}
	for (const pin of now) {
		// A pin that sounded stays a buzzer (playing a note sets it as an output for a while).
		if (seen.get(pin.pin)?.kind === "tone" && pin.kind === "output") shown.set(pin.pin, { pin: pin.pin, kind: "tone", frequency: 0 });
		else seen.set(pin.pin, pin);
	}
	return [...shown.values()].sort((a, b) => pinOrder(a.pin) - pinOrder(b.pin));
}

/** Whether the program uses the Phone blocks (the virtual phone is then shown). */
export const usesPhone = (projectJson: string): boolean => projectJson.includes('"phone_');

/** Value of an analog slider as the blocks give it: a potentiometer or a light sensor reads 0 to 100. */
export const percent = (value: number): number => Math.round((value * 100) / 1023);
