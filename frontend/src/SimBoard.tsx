// The virtual board drawn as the project's wiring diagram (made by the core, core/src/wiring_svg.cpp):
// its LEDs light up, the servo arm and the stepper shaft turn, the screens show their text, and the
// buttons (and the joystick, PIR and obstacle sensors) are pressed with the mouse, right on the drawing.
//
// The core tags the drawing: each component is a group data-part="<kind>|<pins>" (the key of its
// part in simulation.ts), and the pieces that move have data-sim="led", "halo", "press", "arm"...
import { useEffect, useMemo, useRef } from "react";

import type { SimInput, SimState } from "@/bridge";
import type { SimPart } from "@/simulation";
import "@/SimBoard.css";

interface SimBoardProps {
	/** The wiring diagram of the project (WiringPlan.svg). */
	svg: string;
	parts: SimPart[];
	state: SimState | null;
	/** Text of the diagram for screen readers. */
	label: string;
	onInput(...input: SimInput): void;
}

const set = (element: Element | null | undefined, attribute: string, value: string) => element?.setAttribute(attribute, value);

/** Turns an element of the drawing around its centre (data-cx, data-cy given by the core). */
function rotate(element: Element | null | undefined, degrees: number) {
	if (!element) return;
	set(element, "transform", `rotate(${degrees} ${element.getAttribute("data-cx") ?? 0} ${element.getAttribute("data-cy") ?? 0})`);
}

/** Lights an LED of the drawing: its halo shows, the LED gets brighter. */
function light(group: Element | null, level: number, colour?: string) {
	if (!group) return;
	const halo = group.querySelector<SVGElement>('[data-sim="halo"]');
	if (halo) {
		set(halo, "opacity", String(Math.min(1, level / 255)));
		if (colour) halo.style.color = colour;
	}
	const led = group.querySelector<SVGElement>('[data-sim="led"]');
	if (led) led.style.filter = level > 0 ? `brightness(${1 + (0.7 * level) / 255})` : "";
}

function writeLines(group: Element | null, lines: string[]) {
	group?.querySelectorAll('[data-sim="line"]').forEach((line) => {
		line.textContent = lines[Number(line.getAttribute("data-row") ?? 1) - 1] ?? "";
	});
}

/** The group of the drawing that stands for `part` (a joystick's button belongs to the joystick). */
function groupOf(root: ParentNode, part: SimPart): Element | null {
	const key = part.key.replace(/\|(X_PIN|Y_PIN|SW_PIN)$/, "");
	return root.querySelector(`[data-part="${key.replace(/["\\]/g, "\\$&")}"]`);
}

/** Shows the state of the virtual board on the drawing. */
export function applyState(root: ParentNode, parts: SimPart[], state: SimState | null) {
	for (const part of parts) {
		const group = groupOf(root, part);
		if (!group) continue;
		switch (part.kind) {
			case "led":
				light(group, part.level);
				break;
			case "rgb": {
				const [red, green, blue] = part.color;
				const level = Math.max(red, green, blue);
				light(group, level, `rgb(${red}, ${green}, ${blue})`);
				const body = group.querySelector<SVGElement>('[data-sim="led"]');
				if (body) body.style.fill = level > 0 ? `rgb(${red}, ${green}, ${blue})` : "";
				break;
			}
			case "button":
				group.toggleAttribute("data-pressed", part.pressed);
				set(group.querySelector('[data-sim="indicator"]'), "fill", part.pressed ? "#E53935" : "#5D1F1F");
				break;
			case "buzzer":
				set(group.querySelector('[data-sim="sound"]'), "opacity", part.frequency > 0 ? "1" : "0");
				group.toggleAttribute("data-sounding", part.frequency > 0);
				break;
			case "servo":
				rotate(group.querySelector('[data-sim="arm"]'), 90 - part.angle); // drawn at 90°, pointing up
				break;
			case "stepper":
				rotate(group.querySelector('[data-sim="shaft"]'), part.angle);
				break;
			case "slider":
				// A potentiometer's knob turns from -135° to +135° (drawn in the middle).
				if (part.sensor === "potentiometer") rotate(group.querySelector('[data-sim="knob"]'), (part.value / 1023) * 270 - 135);
				break;
			case "screen": {
				const written = part.lines.some((line) => line.trim() !== "");
				const sample = group.querySelector<SVGElement>('[data-sim="sample"]');
				if (sample) sample.style.display = written ? "none" : "";
				writeLines(group, part.lines); // empty, like a real screen, until the program writes
				break;
			}
			case "digits": {
				const digits = group.querySelector('[data-sim="digits"]');
				if (digits) digits.textContent = part.text;
				break;
			}
			case "strip":
				group.querySelectorAll<SVGElement>('[data-sim="pixel"]').forEach((pixel) => {
					const colour = part.colors[Number(pixel.getAttribute("data-index") ?? 0)];
					const lit = colour && colour.some((channel) => channel > 0);
					pixel.style.fill = lit ? `rgb(${colour[0]}, ${colour[1]}, ${colour[2]})` : "#424242";
				});
				break;
			case "matrix":
				group.querySelectorAll<SVGElement>('[data-sim="dot"]').forEach((dot) => {
					const row = part.rows[Number(dot.getAttribute("data-row") ?? 0)] ?? 0;
					const lit = (row >> (7 - Number(dot.getAttribute("data-col") ?? 0))) & 1;
					dot.style.fill = lit ? "#F44336" : "#4E1C1A";
				});
				break;
			default:
				break;
		}
	}
	// The board's own "L" LED (its built-in LED): not a component of the project.
	root.querySelectorAll('[data-part^="board-led|"]').forEach((group) => {
		const pin = group.getAttribute("data-part")?.split("|")[1] ?? "";
		light(group, state?.outputs[pin]?.level ?? 0);
	});
}

/** The pin pressed by a click on `target`: a button, a joystick's push, a PIR or obstacle sensor. */
export function pressTarget(target: EventTarget | null, parts: SimPart[]): string | null {
	if (!(target instanceof Element)) return null;
	const key = target.closest('[data-sim="press"]')?.closest("[data-part]")?.getAttribute("data-part");
	if (!key) return null;
	const part = parts.find((candidate) => candidate.kind === "button" && (candidate.key === key || candidate.key === `${key}|SW_PIN`));
	return part?.kind === "button" ? part.pin : null;
}

export function SimBoard({ svg, parts, state, label, onInput }: SimBoardProps) {
	const root = useRef<HTMLDivElement>(null);
	const held = useRef<string | null>(null);
	// The drawing is inserted once per diagram: the state then only changes attributes.
	const html = useMemo(() => ({ __html: svg }), [svg]);

	useEffect(() => {
		if (root.current) applyState(root.current, parts, state);
	}, [html, parts, state]);

	useEffect(() => {
		const release = () => {
			if (held.current === null) return;
			onInput("press", held.current, false);
			held.current = null;
		};
		window.addEventListener("pointerup", release);
		window.addEventListener("blur", release);
		return () => {
			window.removeEventListener("pointerup", release);
			window.removeEventListener("blur", release);
			release();
		};
	}, [onInput]);

	return (
		<div
			ref={root}
			className="s2a-simboard"
			role="img"
			aria-label={label}
			onPointerDown={(event) => {
				const pin = pressTarget(event.target, parts);
				if (pin === null) return;
				event.preventDefault();
				held.current = pin;
				onInput("press", pin, true);
			}}
			// The diagram is made by the core from the project (texts escaped there), never typed as HTML.
			dangerouslySetInnerHTML={html}
		/>
	);
}
