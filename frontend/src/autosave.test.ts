import { describe, expect, it } from "vitest";

import { AutosaveTracker, autosaveWarnAfter } from "@/autosave";

// Audit 2026-11-24 (#170): a failed autosave counted as done and was never retried.
describe("autosave tracker", () => {
	it("needs a copy only for edits the host does not hold yet", () => {
		const tracker = new AutosaveTracker();
		expect(tracker.needs(0)).toBe(false);
		expect(tracker.needs(3)).toBe(true);
		tracker.started(3);
		expect(tracker.needs(3)).toBe(false); // already on its way
		tracker.succeeded(3);
		expect(tracker.needs(3)).toBe(false);
		expect(tracker.needs(4)).toBe(true);
	});

	it("retries edits whose autosave failed", () => {
		const tracker = new AutosaveTracker();
		tracker.started(2);
		tracker.failed(2);
		expect(tracker.needs(2)).toBe(true);
	});

	it("warns once, after several failures in a row", () => {
		const tracker = new AutosaveTracker();
		const warnings: boolean[] = [];
		for (let edits = 1; edits <= autosaveWarnAfter + 2; edits += 1) {
			tracker.started(edits);
			warnings.push(tracker.failed(edits));
		}
		expect(warnings.filter(Boolean)).toHaveLength(1);
		expect(warnings[autosaveWarnAfter - 1]).toBe(true);
		// A success starts the count again.
		tracker.started(10);
		tracker.succeeded(10);
		tracker.started(11);
		expect(tracker.failed(11)).toBe(false);
	});

	it("does not autosave while a save of the same edits is in progress", () => {
		const tracker = new AutosaveTracker();
		tracker.started(4); // Save pressed: the file dialog is open
		expect(tracker.needs(4)).toBe(false);
		tracker.released(4); // cancelled
		expect(tracker.needs(4)).toBe(true);
	});

	it("does not warn about copies of a project saved meanwhile", () => {
		const tracker = new AutosaveTracker();
		for (let edits = 1; edits <= autosaveWarnAfter; edits += 1) tracker.started(edits);
		tracker.forget(autosaveWarnAfter);
		for (let edits = 1; edits <= autosaveWarnAfter; edits += 1) expect(tracker.failed(edits)).toBe(false);
	});

	it("never goes back to older edits when a slow autosave ends after a save", () => {
		const tracker = new AutosaveTracker();
		tracker.started(2);
		tracker.forget(5); // the user saved: the host dropped the autosave
		tracker.succeeded(2);
		expect(tracker.needs(5)).toBe(false);
		expect(tracker.needs(6)).toBe(true);
	});
});
