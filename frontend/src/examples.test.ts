import { readdirSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, it } from "vitest";

import { exampleGroups, exampleProject, examples } from "@/examples";
import { projectToWorkspace, unknownBlockTypes } from "@/project/convert";

const projectsDir = resolve(__dirname, "../../examples/projects");

describe("example gallery", () => {
	it("lists every example folder except the all-blocks test project", () => {
		const folders = readdirSync(projectsDir, { withFileTypes: true })
			.filter((entry) => entry.isDirectory() && entry.name !== "all-blocks")
			.map((entry) => entry.name)
			.sort();
		expect(examples.map((example) => example.id).sort()).toEqual(folders);
	});

	it("only holds blocks the editor knows", () => {
		for (const example of examples) {
			expect(unknownBlockTypes(example.project), example.id).toEqual([]);
			expect(() => projectToWorkspace(example.project)).not.toThrow();
		}
	});

	it("names the project in the user's language without changing the example", () => {
		const blink = examples.find((example) => example.id === "blink");
		expect(blink).toBeDefined();
		if (!blink) return;
		const before = blink.project.name;
		expect(exampleProject(blink, "en").name).toBe("Blink");
		expect(exampleProject(blink, "fr").name).toBe("Clignotant");
		expect(blink.project.name).toBe(before);
		expect(blink.wiring).not.toBe("");
	});

	it("groups the variants of an example in one card", () => {
		const thermometer = exampleGroups.find((group) => group[0]?.group === "thermometer");
		expect(thermometer?.map((example) => example.id)).toEqual(["thermometer", "thermometer-lcd16"]);
		const [i2c, parallel] = thermometer ?? [];
		expect(i2c?.variant?.fr).toBe("Écran I2C (4 broches)");
		expect(parallel?.parts.en).toContain("16 pins");
		expect(parallel?.wiring).not.toBe(i2c?.wiring);
		const wireless = exampleGroups.find((group) => group[0]?.group === "wireless-station");
		expect(wireless?.map((example) => example.variant?.en)).toEqual(["Over Wi-Fi", "Over Bluetooth"]);
		expect(wireless?.every((example) => example.project.board === "esp32")).toBe(true);
		expect(exampleGroups.flat()).toEqual(examples);
		expect(exampleGroups.filter((group) => group.length === 1).every((group) => group[0]?.variant === undefined)).toBe(true);
	});
});
