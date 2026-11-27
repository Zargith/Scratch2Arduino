// Flows of the whole editor (audit #201): rules of the project that only show with App mounted.
import { act } from "react";
import * as Blockly from "scratch-blocks";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { Outcome } from "@/bridge";
import type { Project } from "@/project/convert";
import { defaultHost, installHost, mountApp, startupProject, waitUntil, type MountedApp } from "@/test/app";

const project = (name: string, ...blocks: Project["scripts"][number]["blocks"]): Project => ({
	format: "scratch2arduino-project",
	formatVersion: 2,
	name,
	board: "uno",
	variables: [],
	scripts: [{ x: 40, y: 40, blocks: [{ id: "start", type: "events_when_program_starts" }, ...blocks] }],
});

let mounted: MountedApp | null = null;
afterEach(async () => {
	await mounted?.unmount();
	mounted = null;
	vi.restoreAllMocks();
});

describe("App flows", () => {
	// CLAUDE.md: "The interface must not throw errors when the user is building his project, but
	// only when the user is trying to build/run it."
	it("says nothing about a broken program until Check, then shows the faulty block", async () => {
		const builds: unknown[] = [];
		const outcome: Outcome = { ok: false, message: "Le bloc \"répéter\" est vide.", blockIds: ["rep"] };
		installHost({
			...defaultHost(),
			s2a_startup_project: startupProject(project("Cassé", { id: "rep", type: "control_repeat" })),
			s2a_build: async (...args: never[]) => {
				builds.push(args);
				return outcome;
			},
		});
		const highlighted = vi.spyOn(Blockly.WorkspaceSvg.prototype, "highlightBlock");
		mounted = await mountApp();
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("rep") != null);
		await act(async () => new Promise((done) => setTimeout(done, 200)));

		expect(builds).toHaveLength(0);
		expect(mounted.text()).not.toContain(outcome.message);
		expect(highlighted.mock.calls.filter(([id]) => id === "rep")).toHaveLength(0);

		await mounted.click("Vérifier");
		await waitUntil(() => mounted!.text().includes(outcome.message));
		expect(builds).toHaveLength(1);
		expect(highlighted).toHaveBeenCalledWith("rep", true);
		expect(mounted.errors).toEqual([]);
	}, 60_000);

	it("offers the project that was not saved at start-up and brings it back", async () => {
		installHost({
			...defaultHost(),
			s2a_autosaved: async () => ({
				project: project("Robot perdu", { id: "led", type: "pins_builtin_led", fields: { STATE: "ON" } }),
				path: "",
				savedAt: 1_764_000_000,
			}),
		});
		mounted = await mountApp();
		await waitUntil(() => mounted!.text().includes("Robot perdu"));
		await mounted.click("Récupérer");
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("led") != null);
		expect(mounted.text()).toContain("Robot perdu");
		expect(mounted.errors).toEqual([]);
	}, 60_000);

	it("does not offer the autosave over a project given at start-up", async () => {
		// Audit #170: a save would have replaced the project just opened with the old copy.
		installHost({
			...defaultHost(),
			s2a_autosaved: async () => ({ project: project("Vieille copie"), path: "", savedAt: 1_764_000_000 }),
			s2a_startup_project: startupProject(project("Ouvert", { id: "opened-led", type: "pins_builtin_led", fields: { STATE: "ON" } })),
		});
		mounted = await mountApp();
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("opened-led") != null, "the opened project");
		await act(async () => new Promise((done) => setTimeout(done, 300)));
		expect(mounted.text()).not.toContain("Vieille copie");
	}, 60_000);
});
