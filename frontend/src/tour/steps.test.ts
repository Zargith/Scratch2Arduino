import { describe, expect, it } from "vitest";

import { findBlock } from "@/catalog";
import type { Project, ProjectBlock } from "@/project/convert";
import { MELODY_FUNCTION, melodyCode, steps, type TourContext } from "@/tour/steps";

const blocks = (...stack: ProjectBlock[]): Project => ({
	format: "scratch2arduino-project",
	formatVersion: 1,
	name: "Ma sonnette",
	board: "uno",
	variables: [],
	scripts: [{ x: 0, y: 0, blocks: [{ id: "start", type: "events_when_program_starts" }, ...stack] }],
});

const context = (project: Project, changes: Partial<TourContext> = {}): TourContext => ({
	project,
	view: "blocks",
	wiringShown: false,
	live: false,
	simulated: false,
	pressed: [],
	userCode: null,
	checks: 0,
	...changes,
});

const step = (id: string) => {
	const found = steps.find((candidate) => candidate.id === id);
	if (!found) throw new Error(id);
	return found;
};

const done = (id: string, now: TourContext, memory: Record<string, unknown> = {}, start: TourContext = now) =>
	step(id).done(now, memory, start);

/** The doorbell as the tutorial builds it, up to the LED (and the extra blocks of `then`). */
const doorbell = (...then: ProjectBlock[]): Project =>
	blocks({
		id: "forever",
		type: "control_forever",
		statements: {
			SUBSTACK: [
				{
					id: "if",
					type: "control_if_else",
					inputs: { CONDITION: { block: { id: "button", type: "sensors_button_pressed", fields: { PIN: "2" } } } },
					statements: {
						SUBSTACK: [{ id: "on", type: "pins_digital_write", fields: { PIN: "8", STATE: "HIGH" } }, ...then],
						SUBSTACK2: [{ id: "off", type: "pins_digital_write", fields: { PIN: "8", STATE: "LOW" } }],
					},
				},
			],
		},
	});

describe("interactive tutorial", () => {
	it("has unique steps, in both languages, pointing at blocks that exist", () => {
		expect(new Set(steps.map((candidate) => candidate.id)).size).toBe(steps.length);
		for (const candidate of steps) {
			for (const text of [candidate.title, candidate.text]) {
				expect(text.fr.length, candidate.id).toBeGreaterThan(0);
				expect(text.en.length, candidate.id).toBeGreaterThan(0);
				// No typography that looks machine-made (see CONTRIBUTING).
				expect(text.fr + text.en, candidate.id).not.toMatch(/[—–“”’«»…→]/);
			}
		}
		for (const type of ["control_forever", "control_if_else", "sensors_button_pressed", "pins_digital_write", "sound_play_tone", "mycode_call"]) {
			expect(findBlock(type), type).toBeDefined();
		}
	});

	it("sees each block step done only when the blocks are in place", () => {
		expect(done("start", context(blocks()))).toBe(true);
		expect(done("forever", context(blocks()))).toBe(false);
		expect(done("forever", context(blocks({ id: "forever", type: "control_forever" })))).toBe(true);
		expect(done("if", context(blocks({ id: "forever", type: "control_forever" })))).toBe(false);
		expect(done("button", context(doorbell()))).toBe(true);
		expect(done("led", context(doorbell()))).toBe(true);
		// The LED on pin 13 (the block's default) is not the doorbell's LED.
		const wrongPin = doorbell();
		const decision = wrongPin.scripts[0]!.blocks[1]!.statements!.SUBSTACK![0]!;
		decision.statements!.SUBSTACK![0]!.fields!.PIN = "13";
		expect(done("led", context(wrongPin))).toBe(false);
		expect(done("buzzer", context(doorbell()))).toBe(false);
		expect(done("buzzer", context(doorbell({ id: "beep", type: "sound_play_tone", fields: { PIN: "6" } })))).toBe(true);
		expect(done("melody-block", context(doorbell({ id: "tune", type: "mycode_call", fields: { NAME: MELODY_FUNCTION } })))).toBe(true);
	});

	it("follows the editor: wiring, Live on the virtual board, the virtual button, checks", () => {
		const project = doorbell();
		expect(done("wiring", context(project, { wiringShown: true }))).toBe(true);
		expect(done("live", context(project, { live: true, simulated: false }))).toBe(false);
		expect(done("live", context(project, { live: true, simulated: true }))).toBe(true);
		// The button was pressed once: the step stays done after it is let go.
		const memory = {};
		expect(done("press", context(project, { live: true, simulated: true, pressed: ["2"] }), memory)).toBe(true);
		expect(done("press", context(project, { live: true, simulated: true }), memory)).toBe(true);
		expect(done("press", context(project, { live: true }), {})).toBe(false);
		const start = context(project, { checks: 2 });
		expect(done("check", context(project, { checks: 2 }), {}, start)).toBe(false);
		expect(done("check", context(project, { checks: 3 }), {}, start)).toBe(true);
	});

	it("writes the tune in C++ and sees it converted to Python", () => {
		for (const language of ["fr", "en"] as const) {
			const code = melodyCode(language);
			expect(code).toContain(`void ${MELODY_FUNCTION}() {`);
			expect(code.match(/tone\(6, /g)).toHaveLength(3);
		}
		const cpp = { language: "cpp" as const, code: melodyCode("fr") };
		expect(done("code", context(doorbell(), { userCode: null }))).toBe(false);
		expect(done("code", context(doorbell(), { userCode: cpp }))).toBe(true);
		expect(done("python", context(doorbell(), { userCode: cpp }))).toBe(false);
		const python = { language: "python" as const, code: `def ${MELODY_FUNCTION}():\n    tone(6, 523, 150)\n` };
		expect(done("python", context(doorbell(), { userCode: python }))).toBe(true);
		expect(done("live-melody", context(doorbell(), { userCode: python, live: true, pressed: ["2"] }), {})).toBe(true);
	});
});
