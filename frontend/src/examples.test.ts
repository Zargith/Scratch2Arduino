import { readdirSync } from "node:fs";
import { resolve } from "node:path";

import { describe, expect, it } from "vitest";

import { exampleProject, examples } from "@/examples";
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
});
