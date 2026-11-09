import { ActionIcon, Badge, Button, Group, Modal, ScrollArea, Stack, Text, TextInput, Tooltip } from "@mantine/core";
import { IconDownload, IconPlus, IconTrash, IconUpload } from "@tabler/icons-react";
import { useEffect, useState } from "react";

import type { MessageKey } from "@/i18n";
import { countBlocks, type SavedGroup } from "@/myBlocks";

interface LibraryProps {
	opened: boolean;
	groups: SavedGroup[];
	t(key: MessageKey, params?: Record<string, string>): string;
	onInsert(group: SavedGroup): void;
	onRename(group: SavedGroup, name: string): void;
	onRemove(group: SavedGroup): void;
	onExport(): void;
	onImport(): void;
	onClose(): void;
}

/** One group: its name can be edited in place (saved when the field is left or Enter pressed). */
function GroupRow({ group, t, onInsert, onRename, onRemove }: Pick<LibraryProps, "t" | "onInsert" | "onRename" | "onRemove"> & { group: SavedGroup }) {
	const [name, setName] = useState(group.name);
	useEffect(() => setName(group.name), [group.name]);
	const commit = () => {
		const trimmed = name.trim();
		if (trimmed === "") setName(group.name);
		else if (trimmed !== group.name) onRename(group, trimmed);
	};
	return (
		<Group gap="sm" wrap="nowrap">
			<TextInput
				aria-label={t("library.name")}
				value={name}
				onChange={(event) => setName(event.currentTarget.value)}
				onBlur={commit}
				onKeyDown={(event) => {
					if (event.key === "Enter") event.currentTarget.blur();
				}}
				style={{ flex: 1 }}
			/>
			<Badge variant="light" color="gray" style={{ flexShrink: 0 }}>
				{t("library.blocks", { count: String(countBlocks(group.blocks)) })}
			</Badge>
			<Button size="xs" variant="light" leftSection={<IconPlus size={14} />} onClick={() => onInsert(group)}>
				{t("library.insert")}
			</Button>
			<Tooltip label={t("library.delete")}>
				<ActionIcon
					variant="subtle"
					color="red"
					aria-label={t("library.delete")}
					onClick={() => {
						if (window.confirm(t("library.confirm_delete", { name: group.name }))) onRemove(group);
					}}
				>
					<IconTrash size={16} />
				</ActionIcon>
			</Tooltip>
		</Group>
	);
}

/** "My blocks": rename, delete, add to the project, export and import. */
export function Library({ opened, groups, t, onInsert, onRename, onRemove, onExport, onImport, onClose }: LibraryProps) {
	return (
		<Modal opened={opened} onClose={onClose} title={t("library.title")} size="lg">
			<Stack gap="sm">
				{groups.length === 0 ? (
					<Text size="sm" c="dimmed">
						{t("library.empty")}
					</Text>
				) : (
					<ScrollArea.Autosize mah="55vh">
						<Stack gap="xs">
							{groups.map((group) => (
								<GroupRow key={group.id} group={group} t={t} onInsert={onInsert} onRename={onRename} onRemove={onRemove} />
							))}
						</Stack>
					</ScrollArea.Autosize>
				)}
				<Text size="xs" c="dimmed">
					{t("library.help")}
				</Text>
				<Group justify="flex-end" gap="sm">
					<Button variant="default" leftSection={<IconUpload size={16} />} onClick={onImport}>
						{t("library.import")}
					</Button>
					<Button variant="default" leftSection={<IconDownload size={16} />} onClick={onExport} disabled={groups.length === 0}>
						{t("library.export")}
					</Button>
				</Group>
			</Stack>
		</Modal>
	);
}
