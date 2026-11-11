import { describe, expect, it } from "vitest";

import { docSource, headingAnchor, pictureUrl, renderDoc, resolveLink, tutorialFor, tutorials } from "@/docs";
import { exampleGroups } from "@/examples";

const pages = (language: "fr" | "en") => ["README.md", "guide.md", "teachers.md", ...tutorials.map((tutorial) => tutorial.path)].filter(
	(path) => docSource(language, path) !== undefined
);

describe("bundled documentation", () => {
	it("has the 8 tutorials of the learner progression, in both languages", () => {
		expect(tutorials.map((tutorial) => tutorial.step)).toEqual([1, 2, 3, 4, 5, 6, 7, 8]);
		for (const tutorial of tutorials) {
			expect(docSource("fr", tutorial.path), tutorial.path).toBeDefined();
			expect(docSource("en", tutorial.path), tutorial.path).toBeDefined();
		}
	});

	it("matches every tutorial with an example of the gallery", () => {
		const groups = exampleGroups.map((group) => group[0]?.group);
		for (const tutorial of tutorials) expect(groups, tutorial.path).toContain(tutorial.exampleId);
		expect(tutorialFor("blink")?.path).toBe("tutorials/01-blink.md");
		expect(tutorialFor("esp32-dimmer")).toBeUndefined();
	});

	it("shows the wiring diagram of every picture", () => {
		for (const language of ["fr", "en"] as const) {
			for (const path of pages(language)) {
				for (const [, href] of (docSource(language, path) ?? "").matchAll(/!\[[^\]]*\]\(([^)]+)\)/g)) {
					expect(pictureUrl(href ?? ""), `${language}/${path}: ${href}`).not.toBe("");
				}
			}
		}
		expect(renderDoc("fr", "tutorials/02-traffic-light.md")).toMatch(/<img src="[^"]+"/);
	});

	it("follows the links between pages, and their headings", () => {
		for (const language of ["fr", "en"] as const) {
			for (const path of pages(language)) {
				const html = renderDoc(language, path);
				for (const [, href] of html.matchAll(/<a href="([^"]+)"/g)) {
					const link = resolveLink(language, path, (href ?? "").replace(/&amp;/g, "&"));
					if (link.kind === "outside") continue;
					const target = link.kind === "page" ? renderDoc(link.language, link.path) : html;
					if (link.anchor === "") continue;
					const headings = [...target.matchAll(/<h[1-4][^>]*>(.*?)<\/h[1-4]>/g)].map(([, text]) =>
						headingAnchor((text ?? "").replace(/<[^>]+>/g, "").replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, "&"))
					);
					expect(headings, `${language}/${path} → ${href}`).toContain(link.anchor);
				}
			}
		}
	});

	it("resolves relative links, across languages, and leaves the repository files outside", () => {
		expect(resolveLink("fr", "tutorials/01-blink.md", "02-traffic-light.md")).toEqual({
			kind: "page",
			language: "fr",
			path: "tutorials/02-traffic-light.md",
			anchor: "",
		});
		expect(resolveLink("fr", "tutorials/01-blink.md", "../guide.md#15-en-cas-de-problème")).toMatchObject({
			kind: "page",
			path: "guide.md",
			anchor: "15-en-cas-de-problème",
		});
		expect(resolveLink("fr", "guide.md", "../en/guide.md")).toMatchObject({ kind: "page", language: "en", path: "guide.md" });
		expect(resolveLink("en", "guide.md", "#7-live-mode")).toEqual({ kind: "anchor", anchor: "7-live-mode" });
		expect(resolveLink("en", "guide.md", "../../CONTRIBUTING.md")).toEqual({ kind: "outside" });
		expect(resolveLink("en", "guide.md", "https://example.org")).toEqual({ kind: "outside" });
	});

	it("gives headings the anchors of GitHub", () => {
		expect(headingAnchor("10. Variables, fonctions et « Mes blocs »")).toBe("10-variables-fonctions-et--mes-blocs-");
		expect(headingAnchor('10. Variables, functions and "My blocks"')).toBe("10-variables-functions-and-my-blocks");
		expect(headingAnchor("14. En cas de problème")).toBe("15-en-cas-de-problème");
	});
});
