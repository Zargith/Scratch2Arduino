// Flows of the whole editor (audit #201): rules of the project that only show with App mounted.
import { act } from "react";
import * as Blockly from "scratch-blocks";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { Outcome } from "@/bridge";
import type { Project } from "@/project/convert";
import { CREATE_FUNCTION_CALLBACK } from "@/blocks/definitions";
import { defaultHost, installHost, mountApp, startupProject, waitUntil, type MountedApp } from "@/test/app";
import { button, input, typeInto } from "@/test/render";

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

	it("creates a function from the palette with its inputs", async () => {
		installHost({ ...defaultHost(), s2a_startup_project: startupProject(project("Fonctions", { id: "fn-led", type: "pins_builtin_led", fields: { STATE: "ON" } })) });
		mounted = await mountApp();
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("fn-led") != null, "the project");
		const workspace = Blockly.getMainWorkspace() as Blockly.WorkspaceSvg;
		await act(async () => workspace.getButtonCallback(CREATE_FUNCTION_CALLBACK)?.(undefined as never));
		await waitUntil(() => input("Nom de la fonction") !== undefined, "the function window");
		typeInto(input("Nom de la fonction")!, "biper");
		await act(async () => button("Ajouter une entrée")!.click());
		typeInto(input("Entrée 1")!, "fois");
		await act(async () => button("Valider")!.click());
		await waitUntil(
			() => workspace.getVariableMap().getVariablesOfType("function").some((variable) => variable.getName() === "biper"),
			"the new function"
		);
		expect(mounted.errors).toEqual([]);
	}, 60_000);

	it("saves blocks to My blocks under the name typed", async () => {
		const added: unknown[][] = [];
		installHost({
			...defaultHost(),
			s2a_startup_project: startupProject(project("Groupe", { id: "group-led", type: "pins_builtin_led", fields: { STATE: "ON" } })),
			s2a_library_add: async (...args: never[]) => {
				added.push(args);
				return { id: "g1", name: "Allumer", blocks: args[1] };
			},
		});
		mounted = await mountApp();
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("group-led") != null, "the project");
		const block = Blockly.getMainWorkspace()!.getBlockById("group-led") as Blockly.BlockSvg;
		const item = Blockly.ContextMenuRegistry.registry.getItem("s2a_save_to_library");
		expect(item).not.toBeNull();
		await act(async () => (item as unknown as { callback: (scope: { block: Blockly.BlockSvg }) => void }).callback({ block }));
		await waitUntil(() => document.querySelector<HTMLInputElement>("[data-autofocus]") !== null, "the name prompt");
		typeInto(document.querySelector<HTMLInputElement>("[data-autofocus]")!, "Allumer");
		await act(async () => button("Valider")!.click());
		await waitUntil(() => added.length > 0, "the group saved");
		expect(added[0]![0]).toBe("Allumer");
	}, 60_000);
});
