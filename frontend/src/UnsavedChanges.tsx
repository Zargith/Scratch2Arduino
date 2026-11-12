// Asked before Scratch2Arduino closes (update install) while the project has unsaved changes.
import { Button, Group, Modal, Text } from "@mantine/core";

import type { MessageKey } from "@/i18n";

export type UnsavedChoice = "save" | "discard" | "cancel";

interface UnsavedChangesProps {
	opened: boolean;
	t(key: MessageKey, params?: Record<string, string>): string;
	onChoice(choice: UnsavedChoice): void;
}

export function UnsavedChanges({ opened, t, onChoice }: UnsavedChangesProps) {
	return (
		<Modal opened={opened} onClose={() => onChoice("cancel")} title={t("unsaved.title")} centered zIndex={400}>
			<Text size="sm">{t("unsaved.update")}</Text>
			<Group justify="flex-end" gap="sm" mt="lg">
				<Button variant="default" onClick={() => onChoice("cancel")}>
					{t("unsaved.cancel")}
				</Button>
				<Button variant="light" color="red" onClick={() => onChoice("discard")}>
					{t("unsaved.discard")}
				</Button>
				<Button data-autofocus onClick={() => onChoice("save")}>
					{t("unsaved.save")}
				</Button>
			</Group>
		</Modal>
	);
}
