// Code editor of the C++, Python and JavaScript modes (CodeMirror 6, bundled offline).
// Mistakes are shown only when the host reports them (Check, Upload, Live), never while typing:
// no linter runs on the text, the problems are set from outside and follow the edits.
import { cpp } from "@codemirror/lang-cpp";
import { javascript } from "@codemirror/lang-javascript";
import { python } from "@codemirror/lang-python";
import { lintGutter, setDiagnostics, type Diagnostic } from "@codemirror/lint";
import { Compartment, EditorState, type Extension } from "@codemirror/state";
import { EditorView } from "@codemirror/view";
import { basicSetup } from "codemirror";
import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";

import type { Language } from "@/catalog";
import { problemRange, type CodeProblem } from "@/program";
import type { ProgramLanguage } from "@/project/convert";

export interface CodeEditorHandle {
	/** Marks the faulty lines (underline, gutter, message on hover) and scrolls to the first one. */
	showProblems(problems: CodeProblem[]): void;
	/** Removes the marks of the last Check. */
	clearProblems(): void;
}

interface CodeEditorProps {
	program: Exclude<ProgramLanguage, "blocks">;
	language: Language;
	/** Code shown when the editor is created; later changes come from the user. */
	initialCode: string;
	onChange(code: string): void;
}

function syntax(program: Exclude<ProgramLanguage, "blocks">): Extension {
	if (program === "python") return python();
	if (program === "javascript") return javascript();
	return cpp();
}

/** French texts of CodeMirror's own panels (search, replace, folding...). */
const frenchPhrases: Record<string, string> = {
	Find: "Chercher",
	Replace: "Remplacer",
	next: "suivant",
	previous: "précédent",
	all: "tout",
	"match case": "respecter la casse",
	"by word": "mot entier",
	regexp: "expression régulière",
	replace: "remplacer",
	"replace all": "tout remplacer",
	close: "fermer",
	"Go to line": "Aller à la ligne",
	go: "aller",
	"Folded lines": "Lignes repliées",
	"Unfolded lines": "Lignes dépliées",
	"Fold line": "Replier la ligne",
	"Unfold line": "Déplier la ligne",
	Diagnostics: "Erreurs",
	"No diagnostics": "Aucune erreur",
};

const look = EditorView.theme({
	"&": { height: "100%", fontSize: "15px" },
	".cm-scroller": { fontFamily: "'Space Mono', monospace", lineHeight: "1.6" },
	".cm-content": { paddingBottom: "40vh" }, // the last lines are not hidden by the status bar
});

export const CodeEditor = forwardRef<CodeEditorHandle, CodeEditorProps>(function CodeEditor(
	{ program, language, initialCode, onChange },
	ref
) {
	const host = useRef<HTMLDivElement>(null);
	const view = useRef<EditorView | null>(null);
	const syntaxSlot = useRef(new Compartment());
	const phrasesSlot = useRef(new Compartment());
	const onChangeRef = useRef(onChange);
	onChangeRef.current = onChange;

	// Created once; the language and the texts are switched in place.
	useEffect(() => {
		if (!host.current) return;
		const editor = new EditorView({
			parent: host.current,
			state: EditorState.create({
				doc: initialCode,
				extensions: [
					basicSetup,
					lintGutter(),
					look,
					EditorView.lineWrapping,
					syntaxSlot.current.of(syntax(program)),
					phrasesSlot.current.of(EditorState.phrases.of(language === "fr" ? frenchPhrases : {})),
					EditorView.updateListener.of((update) => {
						if (update.docChanged) onChangeRef.current(update.state.doc.toString());
					}),
				],
			}),
		});
		view.current = editor;
		return () => {
			editor.destroy();
			view.current = null;
		};
		// The initial code and language are read once: the editor is recreated with a new key.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []);

	useEffect(() => {
		view.current?.dispatch({ effects: syntaxSlot.current.reconfigure(syntax(program)) });
	}, [program]);

	useEffect(() => {
		view.current?.dispatch({
			effects: phrasesSlot.current.reconfigure(EditorState.phrases.of(language === "fr" ? frenchPhrases : {})),
		});
	}, [language]);

	useImperativeHandle(ref, () => ({
		showProblems: (problems) => {
			const editor = view.current;
			if (!editor) return;
			const code = editor.state.doc.toString();
			const diagnostics: Diagnostic[] = problems.map((problem) => ({
				...problemRange(code, problem),
				severity: "error",
				message: problem.message,
			}));
			editor.dispatch(setDiagnostics(editor.state, diagnostics));
			if (diagnostics.length > 0) {
				editor.dispatch({
					selection: { anchor: diagnostics[0].from },
					effects: EditorView.scrollIntoView(diagnostics[0].from, { y: "center" }),
				});
				editor.focus();
			}
		},
		clearProblems: () => {
			const editor = view.current;
			if (editor) editor.dispatch(setDiagnostics(editor.state, []));
		},
	}));

	return <div ref={host} style={{ height: "100%", overflow: "hidden" }} data-testid="code-editor" />;
});
