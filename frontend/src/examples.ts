// Example projects shipped inside the editor (examples/projects): the project, its wiring
// diagram and a short text in both languages. Vite bundles them, so the gallery works
// offline and without the native host.
import type { Language } from "@/catalog";
import type { Project } from "@/project/convert";

import gallery from "../../examples/projects/gallery.json";

type Texts = Record<Language, string>;

export interface Example {
	id: string;
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

/** The examples of gallery.json that have both a project and a diagram, in the learner order. */
export const examples: Example[] = gallery.examples.flatMap((entry) => {
	const project = projects[`${folder(entry.id)}/project.json`];
	const wiring = diagrams[`${folder(entry.id)}/wiring.png`];
	return project && wiring ? [{ ...entry, project, wiring }] : [];
});

/** A copy of the example's project, named in the user's language. */
export function exampleProject(example: Example, language: Language): Project {
	return { ...structuredClone(example.project), name: example.title[language] };
}
