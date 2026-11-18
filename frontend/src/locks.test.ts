import * as Blockly from "scratch-blocks";
import { describe, expect, it } from "vitest";

import { defineBlocks } from "@/blocks/definitions";
import { applyLocks } from "@/locks";

describe("tutorial locks", () => {
	it("locks the chosen blocks and the shadows in their slots, and unlocks the others", () => {
		defineBlocks("fr");
		const workspace = new Blockly.Workspace();
		Blockly.serialization.workspaces.load(
			{
				blocks: {
					languageVersion: 0,
					blocks: [
						{
							type: "events_when_program_starts",
							id: "start",
							next: {
								block: {
									type: "control_repeat",
									id: "repeat",
									inputs: {
										TIMES: { shadow: { type: "math_whole_number", id: "times", fields: { NUM: "3" } } },
										SUBSTACK: { block: { type: "control_forever", id: "inner" } },
									},
								},
							},
						},
					],
				},
			} as Parameters<typeof Blockly.serialization.workspaces.load>[0],
			workspace
		);
		const block = (id: string) => workspace.getBlockById(id)!;

		applyLocks(workspace, ["start", "repeat"]);
		for (const id of ["start", "repeat"]) {
			expect(block(id).isMovable(), id).toBe(false);
			expect(block(id).isDeletable(), id).toBe(false);
			expect(block(id).isEditable(), id).toBe(false);
		}
		expect(block("times").isEditable()).toBe(false); // the number of the locked "repeat"
		expect(block("inner").isMovable()).toBe(true);
		expect(block("inner").isDeletable()).toBe(true);

		// A lock restored with the saved workspace goes away when the block is no longer listed.
		applyLocks(workspace, ["start"]);
		expect(block("repeat").isMovable()).toBe(true);
		expect(block("repeat").isEditable()).toBe(true);
		expect(block("times").isEditable()).toBe(true);
		expect(block("start").isMovable()).toBe(false);
		applyLocks(workspace, []);
		expect(block("start").isDeletable()).toBe(true);
	});
});
