import { Button, Group, Modal, Stack, Text, TextInput } from "@mantine/core";
import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";
import * as Blockly from "scratch-blocks";

import {
	blocksTheme,
	CREATE_VARIABLE_CALLBACK,
	defineBlocks,
	registerVariablesCategory,
	setCurrentBoard,
	toolbox,
} from "@/blocks/definitions";
import type { Board, Language } from "@/catalog";
import { translate } from "@/i18n";
import type { WorkspaceState } from "@/project/convert";

type WorkspaceLoadState = Parameters<typeof Blockly.serialization.workspaces.load>[0];

export interface EditorHandle {
	save(): WorkspaceState;
	load(state: WorkspaceState): void;
}

interface EditorProps {
	language: Language;
	board: Board | undefined;
	/** State restored when the editor is created (language switch, project opened). */
	initialState: WorkspaceState;
	onChange(): void;
}

interface PromptRequest {
	message: string;
	value: string;
	resolve(value: string | null): void;
}

export const Editor = forwardRef<EditorHandle, EditorProps>(function Editor(
	{ language, board, initialState, onChange },
	ref
) {
	const container = useRef<HTMLDivElement>(null);
	const workspace = useRef<Blockly.WorkspaceSvg | null>(null);
	const onChangeRef = useRef(onChange);
	onChangeRef.current = onChange;
	const [prompt, setPrompt] = useState<PromptRequest | null>(null);

	useImperativeHandle(ref, () => ({
		save: () => (workspace.current ? (Blockly.serialization.workspaces.save(workspace.current) as WorkspaceState) : {}),
		load: (state) => {
			const target = workspace.current;
			if (!target) return;
			Blockly.Events.disable();
			try {
				target.clear();
				Blockly.serialization.workspaces.load(state as WorkspaceLoadState, target);
			} finally {
				Blockly.Events.enable();
			}
			target.getToolbox()?.refreshSelection();
		},
	}));

	useEffect(() => {
		const element = container.current;
		if (!element) return;
		Blockly.ScratchMsgs.setLocale(language);
		defineBlocks(language);
		const created = Blockly.inject(element, {
			toolbox: toolbox(language),
			theme: blocksTheme(),
			media: "./media/",
			trashcan: true,
			sounds: false,
			zoom: { controls: true, wheel: true, startScale: 0.675 },
			grid: { spacing: 40, length: 2, colour: "#ddd" },
		});
		registerVariablesCategory(created, translate(language, "variables.create"));
		created.registerButtonCallback(CREATE_VARIABLE_CALLBACK, () => {
			Blockly.ScratchVariables.createVariable(created);
		});
		Blockly.ScratchVariables.setPromptHandler((message, defaultValue, callback) => {
			setPrompt({
				message: message || translate(language, "variables.prompt"),
				value: defaultValue,
				resolve: (value) => {
					if (value !== null) callback(value, []);
				},
			});
		});
		Blockly.Events.disable();
		try {
			Blockly.serialization.workspaces.load(initialState as WorkspaceLoadState, created);
		} finally {
			Blockly.Events.enable();
		}
		created.addChangeListener((event: Blockly.Events.Abstract) => {
			if (!event.isUiEvent) onChangeRef.current();
		});
		workspace.current = created;

		const resize = new ResizeObserver(() => Blockly.svgResize(created));
		resize.observe(element);
		return () => {
			resize.disconnect();
			created.dispose();
			workspace.current = null;
		};
		// The editor is rebuilt (with key=language) when the language changes; initialState
		// is only read once.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []);

	useEffect(() => {
		setCurrentBoard(board);
	}, [board]);

	const close = (value: string | null) => {
		prompt?.resolve(value);
		setPrompt(null);
	};

	return (
		<>
			<div ref={container} style={{ position: "absolute", inset: 0 }} />
			<Modal opened={prompt !== null} onClose={() => close(null)} withCloseButton={false} centered>
				<form
					onSubmit={(event) => {
						event.preventDefault();
						close(prompt?.value ?? null);
					}}
				>
					<Stack>
						<Text>{prompt?.message}</Text>
						<TextInput
							data-autofocus
							value={prompt?.value ?? ""}
							onChange={(event) => {
								const value = event.currentTarget.value;
								setPrompt((current) => (current ? { ...current, value } : current));
							}}
						/>
						<Group justify="flex-end">
							<Button variant="default" onClick={() => close(null)}>
								{translate(language, "dialog.cancel")}
							</Button>
							<Button type="submit">{translate(language, "dialog.ok")}</Button>
						</Group>
					</Stack>
				</form>
			</Modal>
		</>
	);
});
