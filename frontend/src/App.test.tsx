import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { EditorView } from "@codemirror/view";
import { act } from "react";
import * as Blockly from "scratch-blocks";
import { afterEach, describe, expect, it } from "vitest";

import { defaultHost, installHost, mountApp, startupProject, waitUntil, type MountedApp } from "@/test/app";

let mounted: MountedApp | null = null;
afterEach(async () => {
	await mounted?.unmount();
	mounted = null;
});

describe("application start-up", () => {
	// Regression: a project with variables and functions, given at start-up (double-clicked
	// file), made Blockly fail while building the palette and left a blank window.
	it("opens a project with variables and functions without stopping", async () => {
		const project = JSON.parse(readFileSync(resolve(__dirname, "../../examples/projects/all-blocks/project.json"), "utf8"));
		expect(project.functions?.length).toBeGreaterThan(0);
		installHost({ ...defaultHost(), s2a_startup_project: startupProject(project) });

		mounted = await mountApp();
		await waitUntil(() => (Blockly.getMainWorkspace()?.getAllBlocks(false).length ?? 0) > 10);
		expect(mounted.errors).toEqual([]);
		expect(mounted.container.innerHTML).not.toBe("");
	}, 60_000);

	// Mac with an Apple chip without Rosetta (1.3.2): a notice with an install button at start-up.
	it("offers to install Rosetta when the Mac needs it", async () => {
		const installed: string[] = [];
		installHost({
			...defaultHost(),
			s2a_rosetta_state: async () => ({ missing: true, message: "Ce Mac a besoin de Rosetta." }),
			s2a_rosetta_install: async (language: never) => {
				installed.push(language);
				return { ok: true, message: "Rosetta est installé." };
			},
		});
		mounted = await mountApp();
		await waitUntil(() => mounted!.text().includes("Ce Mac a besoin de Rosetta."));
		await mounted.click("Installer Rosetta");
		await waitUntil(() => installed.length > 0 && !mounted!.text().includes("Ce Mac a besoin de Rosetta."));
		expect(installed).toEqual(["fr"]);
	}, 60_000);

	// Regression: what was typed in "My code" came back as the starter code after going to the
	// blocks (or to the other tab) and back, until the project was saved and opened again.
	it("keeps what was typed in My code when switching tabs and views", async () => {
		const project = {
			format: "scratch2arduino-project",
			formatVersion: 2,
			name: "Mon code",
			board: "uno",
			variables: [],
			scripts: [{ x: 40, y: 40, blocks: [{ id: "mycode-start", type: "events_when_program_starts" }] }],
			userCode: { language: "python", code: "vitesse = 1\n" },
		};
		installHost({ ...defaultHost(), s2a_startup_project: startupProject(project) });
		mounted = await mountApp();
		const app = mounted;
		const mine = () => {
			const content = app.container.querySelector(".cm-content");
			return content ? EditorView.findFromDOM(content as HTMLElement) : null;
		};
		/** Opens "My code" (from the blocks or from the other tab) and waits for its editor. */
		const showMine = async () => {
			await app.click("Mon code");
			await waitUntil(() => mine() !== null);
		};
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("mycode-start") != null);
		await app.click("Code");
		await showMine();
		expect(mine()?.state.doc.toString()).toBe("vitesse = 1\n");
		await act(async () => mine()?.dispatch({ changes: { from: 0, insert: "# à moi\n" } }));

		await app.click("Code des blocs");
		await showMine();
		expect(mine()?.state.doc.toString()).toBe("# à moi\nvitesse = 1\n");

		await app.click("Blocs");
		await waitUntil(() => Blockly.getMainWorkspace()?.getBlockById("mycode-start") != null);
		await app.click("Code");
		await showMine();
		expect(mine()?.state.doc.toString()).toBe("# à moi\nvitesse = 1\n");
	}, 60_000);
});
