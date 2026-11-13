// Window to create a function or change it: its name and its inputs (numbers given by "call").
import { ActionIcon, Button, Group, Modal, Stack, Text, TextInput } from "@mantine/core";
import { IconPlus, IconTrash } from "@tabler/icons-react";
import { useEffect, useState } from "react";

import { cleanParameterNames, type EditedParameter } from "@/blocks/functions";
import type { Language } from "@/catalog";
import { translate, type MessageKey } from "@/i18n";
import { MAX_FUNCTION_PARAMETERS } from "@/project/convert";

/** What the window starts with: null to create a function. */
export interface FunctionDraft {
	name: string;
	inputs: string[];
}

interface FunctionDialogProps {
	opened: boolean;
	language: Language;
	/** The function being changed; null for a new one. */
	initial: FunctionDraft | null;
	/** True when another function already has this name. */
	nameTaken(name: string): boolean;
	onCancel(): void;
	onDone(name: string, inputs: EditedParameter[]): void;
}

interface Row {
	key: number;
	name: string;
	/** Index of the input it was when the window opened (null: added now). */
	from: number | null;
}

/**
 * The inputs typed in the window, ready for changeFunctionParameters: empty names left out, a
 * name given twice gets a number; each keeps the input it was.
 */
export function editedInputs(rows: { name: string; from: number | null }[]): EditedParameter[] {
	const typed = rows.filter((row) => row.name.trim() !== "");
	const names = cleanParameterNames(typed.map((row) => row.name));
	return names.map((name, i) => ({ name, from: typed[i]?.from ?? null }));
}

export function FunctionDialog({ opened, language, initial, nameTaken, onCancel, onDone }: FunctionDialogProps) {
	const t = (key: MessageKey, params?: Record<string, string>) => translate(language, key, params);
	const [name, setName] = useState("");
	const [rows, setRows] = useState<Row[]>([]);
	const [nextKey, setNextKey] = useState(0);

	useEffect(() => {
		if (!opened) return;
		setName(initial?.name ?? "");
		const start = (initial?.inputs ?? []).map((input, index) => ({ key: index, name: input, from: index }));
		setRows(start);
		setNextKey(start.length);
	}, [opened, initial]);

	const trimmed = name.trim();
	const taken = trimmed !== "" && trimmed !== initial?.name && nameTaken(trimmed);
	const submit = () => {
		if (trimmed === "" || taken) return;
		onDone(trimmed, editedInputs(rows));
	};

	return (
		<Modal
			opened={opened}
			onClose={onCancel}
			centered
			title={initial ? t("functions.edit_title", { name: initial.name }) : t("functions.new_title")}
		>
			<form
				onSubmit={(event) => {
					event.preventDefault();
					submit();
				}}
			>
				<Stack gap="sm">
					<TextInput
						data-autofocus
						label={t("functions.name")}
						value={name}
						onChange={(event) => setName(event.currentTarget.value)}
						error={taken ? t("functions.name_taken", { name: trimmed }) : undefined}
					/>
					<Stack gap={6}>
						<Text size="sm" fw={500}>
							{t("functions.inputs_title")}
						</Text>
						<Text size="xs" c="dimmed">
							{t("functions.inputs_help")}
						</Text>
						{rows.map((row, index) => (
							<Group key={row.key} gap="xs" wrap="nowrap">
								<TextInput
									style={{ flex: 1 }}
									aria-label={t("functions.input_label", { number: String(index + 1) })}
									placeholder={t("functions.input_placeholder")}
									value={row.name}
									onChange={(event) => {
										const value = event.currentTarget.value;
										setRows((current) => current.map((item) => (item.key === row.key ? { ...item, name: value } : item)));
									}}
								/>
								<ActionIcon
									variant="default"
									size="lg"
									aria-label={t("functions.remove_input")}
									onClick={() => setRows((current) => current.filter((item) => item.key !== row.key))}
								>
									<IconTrash size={16} />
								</ActionIcon>
							</Group>
						))}
						<Button
							variant="light"
							leftSection={<IconPlus size={16} />}
							disabled={rows.length >= MAX_FUNCTION_PARAMETERS}
							onClick={() => {
								setRows((current) => [...current, { key: nextKey, name: "", from: null }]);
								setNextKey((key) => key + 1);
							}}
							style={{ alignSelf: "flex-start" }}
						>
							{t("functions.add_input")}
						</Button>
					</Stack>
					<Group justify="flex-end">
						<Button variant="default" onClick={onCancel}>
							{t("dialog.cancel")}
						</Button>
						<Button type="submit" disabled={trimmed === "" || taken}>
							{t("dialog.ok")}
						</Button>
					</Group>
				</Stack>
			</form>
		</Modal>
	);
}
