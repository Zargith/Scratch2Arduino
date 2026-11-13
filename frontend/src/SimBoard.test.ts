import { describe, expect, it } from "vitest";

import type { SimState } from "@/bridge";
import { applyState, pressTarget } from "@/SimBoard";
import type { SimPart } from "@/simulation";

/** A diagram tagged as core/src/wiring_svg.cpp tags it (only the parts the tests need). */
const DIAGRAM = `<svg xmlns="http://www.w3.org/2000/svg">
<g data-part="board-led|13"><circle data-sim="halo" opacity="0"/><rect data-sim="led"/></g>
<g data-part="output|8"><circle data-sim="halo" style="color:#E53935" opacity="0"/><circle data-sim="led" fill="#E53935"/></g>
<g data-part="rgb_led|3|5|6"><circle data-sim="halo" opacity="0"/><circle data-sim="led" fill="#F5F5F5"/></g>
<g data-part="button|2"><g data-sim="press"><rect id="body"/><circle data-sim="cap"/></g></g>
<g data-part="joystick|A0|A1|7"><g data-sim="press"><circle id="stick" data-sim="cap"/></g></g>
<g data-part="servo|9"><polyline data-sim="arm" data-cx="40" data-cy="45"/></g>
<g data-part="stepper|10|11|12|13"><rect data-sim="shaft" data-cx="5" data-cy="6"/></g>
<g data-part="lcd_i2c"><text data-sim="line" data-row="1">Scratch2Arduino</text><text data-sim="line" data-row="2">21</text></g>
<g data-part="matrix|12|10|11"><circle data-sim="dot" data-row="0" data-col="0"/><circle data-sim="dot" data-row="0" data-col="1"/></g>
</svg>`;

function diagram(): HTMLDivElement {
	const root = document.createElement("div");
	root.innerHTML = DIAGRAM;
	return root;
}

const state = (outputs: SimState["outputs"]): SimState =>
	({ outputs, servos: {}, tones: {}, pressed: {}, analog: {} }) as unknown as SimState;

const parts: SimPart[] = [
	{ key: "output|8", name: "LED", kind: "led", pin: "8", level: 255 },
	{ key: "rgb_led|3|5|6", name: "RGB", kind: "rgb", color: [0, 128, 255] },
	{ key: "button|2", name: "Button", kind: "button", pin: "2", pressed: true },
	{ key: "joystick|A0|A1|7|SW_PIN", name: "Joystick", kind: "button", pin: "7", pressed: false },
	{ key: "servo|9", name: "Servo", kind: "servo", pin: "9", angle: 30 },
	{ key: "stepper|10|11|12|13", name: "Stepper", kind: "stepper", pin: "10", angle: -90 },
	{ key: "lcd_i2c", name: "LCD", kind: "screen", screen: "lcd", lines: ["Bonjour", ""] },
	{ key: "matrix|12|10|11", name: "Matrix", kind: "matrix", pin: "12", rows: [0b01000000, 0, 0, 0, 0, 0, 0, 0] },
];

describe("virtual board drawn as the wiring diagram", () => {
	it("lights the LEDs with their halo, the board's L LED included", () => {
		const root = diagram();
		applyState(root, parts, state({ "13": { level: 255, pwm: false } }));
		expect(root.querySelector('[data-part="output|8"] [data-sim="halo"]')?.getAttribute("opacity")).toBe("1");
		expect(root.querySelector('[data-part="board-led|13"] [data-sim="halo"]')?.getAttribute("opacity")).toBe("1");
		const rgb = root.querySelector<SVGElement>('[data-part="rgb_led|3|5|6"] [data-sim="halo"]');
		expect(rgb?.style.color).toBe("rgb(0, 128, 255)");
		expect(rgb?.getAttribute("opacity")).toBe("1");
		applyState(root, [{ ...parts[0]!, level: 0 } as SimPart], state({}));
		expect(root.querySelector('[data-part="output|8"] [data-sim="halo"]')?.getAttribute("opacity")).toBe("0");
		expect(root.querySelector('[data-part="board-led|13"] [data-sim="halo"]')?.getAttribute("opacity")).toBe("0");
	});

	it("moves the servo arm and the stepper shaft, writes the screens and the matrix", () => {
		const root = diagram();
		applyState(root, parts, state({}));
		expect(root.querySelector('[data-sim="arm"]')?.getAttribute("transform")).toBe("rotate(60 40 45)");
		expect(root.querySelector('[data-sim="shaft"]')?.getAttribute("transform")).toBe("rotate(-90 5 6)");
		expect([...root.querySelectorAll('[data-sim="line"]')].map((line) => line.textContent)).toEqual(["Bonjour", ""]);
		const dots = root.querySelectorAll<SVGElement>('[data-sim="dot"]');
		// Row 0 = 0b01000000: only the second dot is lit (left column = high bit).
		expect(dots[0]?.style.fill).not.toBe("");
		expect(dots[1]?.style.fill).not.toBe(dots[0]?.style.fill);
		expect(root.querySelector('[data-part="button|2"]')?.hasAttribute("data-pressed")).toBe(true);
	});

	it("finds the pin pressed by a click on a button or on the joystick's push", () => {
		const root = diagram();
		expect(pressTarget(root.querySelector("#body"), parts)).toBe("2");
		expect(pressTarget(root.querySelector("#stick"), parts)).toBe("7");
		expect(pressTarget(root.querySelector('[data-sim="arm"]'), parts)).toBeNull();
		expect(pressTarget(null, parts)).toBeNull();
	});
});
