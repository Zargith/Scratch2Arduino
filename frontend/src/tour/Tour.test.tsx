import { MantineProvider } from "@mantine/core";
import { act, useState } from "react";
import { createRoot } from "react-dom/client";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { Project } from "@/project/convert";
import { Tour } from "@/tour/Tour";
import { steps, type TourContext } from "@/tour/steps";

const project: Project = {
	format: "scratch2arduino-project",
	formatVersion: 1,
	name: "Ma sonnette",
	board: "uno",
	variables: [],
	scripts: [{ x: 0, y: 0, blocks: [{ id: "start", type: "events_when_program_starts" }] }],
};

const context: TourContext = {
	project,
	view: "blocks",
	wiringShown: false,
	live: false,
	simulated: false,
	pressed: [],
	userCode: null,
	checks: 0,
};

let container: HTMLDivElement | null = null;
afterEach(() => {
	container?.remove();
	container = null;
});

/** The card at step `first`, its Next and Back buttons changing the step. */
function show(first = 0) {
	(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
	window.matchMedia ??= (() => ({
		matches: false,
		addEventListener() {},
		removeEventListener() {},
		addListener() {},
		removeListener() {},
	})) as never;
	container = document.createElement("div");
	document.body.appendChild(container);
	function Shown() {
		const [step, setStep] = useState(first);
		return <Tour language="en" context={context} step={step} onStep={setStep} onAction={vi.fn()} onClose={vi.fn()} />;
	}
	act(() => {
		createRoot(container!).render(
			<MantineProvider>
				<Shown />
			</MantineProvider>
		);
	});
	return container;
}

const button = (root: HTMLElement, text: string) =>
	[...root.querySelectorAll("button")].find((candidate) => candidate.textContent === text)!;

// Audit 2026-11-24 (#211): the tutorial card had no Escape, no focus management, and its step
// changes were not read out.
describe("Tour accessibility", () => {
	it("folds the card with Escape", () => {
		const root = show();
		expect(root.textContent).toContain(steps[0]!.text.en.slice(0, 30));
		const card = root.querySelector<HTMLElement>("[role='dialog']")!;
		act(() => {
			card.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
		});
		expect(root.textContent).not.toContain(steps[0]!.text.en.slice(0, 30));
		expect(root.querySelector("[aria-label='Unfold']")).not.toBeNull();
	});

	it("reads out the step in a status region", () => {
		const root = show();
		const status = root.querySelector("[role='status']")!;
		expect(status.textContent).toContain(`Step 1 of ${steps.length}`);
		expect(status.textContent).toContain(steps[0]!.title.en);
	});

	it("leaves the focus where it is when it was not in the card", () => {
		const root = show();
		const outside = document.createElement("button");
		document.body.appendChild(outside);
		outside.focus();
		act(() => {
			button(root, "Next").click();
		});
		expect(document.activeElement).toBe(outside);
		outside.remove();
	});

	it("reads out that the step is done apart from the step itself", () => {
		// Step 2 ("start") is done when the project has a start script, as here.
		const root = show(1);
		const regions = [...root.querySelectorAll("[role='status']")].map((region) => region.textContent);
		expect(regions[0]).toContain(steps[1]!.title.en);
		expect(regions[1]).toContain("Well done");
	});

	it("moves the focus to the new step's title after Next", () => {
		const root = show();
		const next = button(root, "Next");
		next.focus();
		act(() => {
			next.click();
		});
		expect(document.activeElement?.textContent).toBe(steps[1]!.title.en);
		expect(root.querySelector("[role='status']")!.textContent).toContain(steps[1]!.title.en);
	});
});
