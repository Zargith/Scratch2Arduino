// Example projects shipped inside the editor (examples/projects): the project, its wiring
// diagram and a short text in both languages. Vite bundles them, so the gallery works
// offline and without the native host.
import type { Language } from "@/catalog";
import type { Project } from "@/project/convert";

import gallery from "../../examples/projects/gallery.json";

type Texts = Record<Language, string>;

export interface Example {
	/** Folder of the project (the variant's own folder for an example with variants). */
	id: string;
	/** Id of the gallery entry: the variants of one example share it. */
	group: string;
	/** Name of the variant (which parts it uses), for an example with variants. */
	variant?: Texts;
	title: Texts;
	description: Texts;
	parts: Texts;
	project: Project;
	/** URL of the wiring diagram (inlined into the page when built). */
	wiring: string;
}

const projects = import.meta.glob<Project>("../../examples/projects/*/project.json", { eager: true, import: "default" });
const diagrams = import.meta.glob<string>("../../examples/projects/*/wiring.png", { eager: true, import: "default" });

const folder = (id: string) => `../../examples/projects/${id}`;

interface GalleryVariant {
	id: string;
	label: Texts;
	parts?: Texts;
}

interface GalleryEntry {
	id: string;
	title: Texts;
	description: Texts;
	parts: Texts;
	variants?: GalleryVariant[];
}

/**
 * The examples of gallery.json that have both a project and a diagram, in the learner order. An
 * example with variants (same program, other parts) gives one Example per variant.
 */
export const examples: Example[] = (gallery.examples as GalleryEntry[]).flatMap(({ variants, ...entry }) =>
	(variants ?? [{ id: entry.id }]).flatMap((variant: Partial<GalleryVariant> & { id: string }) => {
		const project = projects[`${folder(variant.id)}/project.json`];
		const wiring = diagrams[`${folder(variant.id)}/wiring.png`];
		if (!project || !wiring) return [];
		return [
			{
				...entry,
				id: variant.id,
				group: entry.id,
				...(variant.label ? { variant: variant.label } : {}),
				parts: variant.parts ?? entry.parts,
				project,
				wiring,
			},
		];
	})
);

/** The examples grouped as the gallery shows them: one card per entry, its variants inside. */
export const exampleGroups: Example[][] = examples.reduce<Example[][]>((groups, example) => {
	const last = groups[groups.length - 1];
	if (last?.[0]?.group === example.group) last.push(example);
	else groups.push([example]);
	return groups;
}, []);

/** A copy of the example's project, named in the user's language. */
export function exampleProject(example: Example, language: Language): Project {
	return { ...structuredClone(example.project), name: example.title[language] };
}
