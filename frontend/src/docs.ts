// The user documentation of docs/fr and docs/en (guide and tutorials), bundled into the editor so
// that it can be read offline next to the blocks. Vite inlines the Markdown files; their pictures
// are the wiring diagrams of the examples, already bundled by examples.ts.
import { Marked, type Tokens } from "marked";

import type { Language } from "@/catalog";
import { examples } from "@/examples";

/** A page, by its path inside docs/<language>: "README.md", "guide.md", "tutorials/01-blink.md". */
export type DocPath = string;

/** The page shown when the panel opens: the list of tutorials. */
export const docsHome: DocPath = "README.md";

const sources = import.meta.glob<string>(["../../docs/fr/**/*.md", "../../docs/en/**/*.md"], {
	query: "?raw",
	import: "default",
	eager: true,
});

const sourcePath = (language: Language, path: DocPath) => `../../docs/${language}/${path}`;

/** The Markdown of a page, or undefined when it is not part of the bundled documentation. */
export function docSource(language: Language, path: DocPath): string | undefined {
	return sources[sourcePath(language, path)];
}

export interface Tutorial {
	path: DocPath;
	/** Its step in the learner progression (1 for Blink). */
	step: number;
	/** The gallery example it builds (tutorials/<step>-<example id>.md). */
	exampleId: string;
}

/** The tutorials, in the learner order (file names are the same in both languages). */
export const tutorials: Tutorial[] = Object.keys(sources)
	.flatMap((key) => {
		const match = /^\.\.\/\.\.\/docs\/fr\/(tutorials\/(\d+)-([a-z0-9-]+)\.md)$/.exec(key);
		return match?.[1] && match[2] && match[3] ? [{ path: match[1], step: Number(match[2]), exampleId: match[3] }] : [];
	})
	.sort((a, b) => a.step - b.step);

/** The tutorial following an example of the gallery (any of its variants), if there is one. */
export function tutorialFor(exampleGroup: string): Tutorial | undefined {
	return tutorials.find((tutorial) => tutorial.exampleId === exampleGroup);
}

/** Where a link of a page leads. */
export type DocLink =
	| { kind: "page"; language: Language; path: DocPath; anchor: string }
	| { kind: "anchor"; anchor: string }
	/** Outside the bundled pages (a web page, a file of the repository): not followed in the editor. */
	| { kind: "outside" };

/** Resolves `href`, written in the page `from` of `language`. */
export function resolveLink(language: Language, from: DocPath, href: string): DocLink {
	if (/^[a-z]+:/i.test(href)) return { kind: "outside" };
	let decoded = href;
	try {
		decoded = decodeURIComponent(href); // the rendered links are percent-encoded ("fen%C3%AAtre")
	} catch {
		// a lone "%": keep the link as written
	}
	const [target = "", anchor = ""] = decoded.split("#", 2);
	if (target === "") return { kind: "anchor", anchor };
	const parts: string[] = [language, ...from.split("/").slice(0, -1)];
	for (const part of target.split("/")) {
		if (part === "..") parts.pop();
		else if (part !== ".") parts.push(part);
	}
	const [pageLanguage, ...rest] = parts;
	if ((pageLanguage !== "fr" && pageLanguage !== "en") || rest.length === 0) return { kind: "outside" };
	const path = rest.join("/");
	if (docSource(pageLanguage, path) === undefined) return { kind: "outside" };
	return { kind: "page", language: pageLanguage, path, anchor };
}

/** The anchor GitHub gives a heading ("10. Variables, fonctions" -> "10-variables-fonctions"). */
export function headingAnchor(text: string): string {
	return text
		.trim()
		.toLowerCase()
		.replace(/[^\p{L}\p{N}\s_-]/gu, "")
		.replace(/\s/g, "-");
}

/** URL of a picture of the documentation: the wiring diagram of an example. */
export function pictureUrl(href: string): string {
	const id = /examples\/projects\/([^/]+)\/wiring\.png$/.exec(href)?.[1];
	return examples.find((example) => example.id === id)?.wiring ?? "";
}

const markdown = new Marked({
	gfm: true,
	walkTokens(token) {
		if (token.type === "image") {
			const image = token as Tokens.Image;
			image.href = pictureUrl(image.href);
		}
	},
});

/** The page as HTML (the bundled Markdown is ours: no user text goes through it). */
export function renderDoc(language: Language, path: DocPath): string {
	const source = docSource(language, path) ?? docSource(language, docsHome) ?? "";
	return markdown.parse(source, { async: false });
}
