// Optional board packages (ESP32, ESP8266...): the list in the settings (install, update,
// remove, with the sizes) and the question asked when a board needs a package not installed yet.
import { Alert, Badge, Button, Card, Code, Group, Loader, Modal, Stack, Text } from "@mantine/core";
import { useState } from "react";

import { bridge, type BoardPackage } from "@/bridge";
import { findBoard, type Language } from "@/catalog";
import type { MessageKey } from "@/i18n";

type Translate = (key: MessageKey, params?: Record<string, string>) => string;

/** A size for people, as the core writes it (decimal units): "1,9 Go" / "1.9 GB", "125 Mo" / "125 MB". */
export function formatSize(bytes: number, language: Language): string {
	const french = language === "fr";
	const rounded = (unit: number) => {
		const tenths = Math.round((bytes * 10) / unit);
		const whole = Math.floor(tenths / 10);
		return tenths < 100 && tenths % 10 !== 0 ? `${whole}${french ? "," : "."}${tenths % 10}` : String(whole);
	};
	if (bytes >= 1e9) return `${rounded(1e9)} ${french ? "Go" : "GB"}`;
	if (bytes >= 1e6) return `${rounded(1e6)} ${french ? "Mo" : "MB"}`;
	return `${Math.ceil(bytes / 1e3)} ${french ? "ko" : "kB"}`;
}

/** The package providing `boardId`, undefined when unknown (or outside the application). */
export function packageOf(packages: BoardPackage[] | null, boardId: string): BoardPackage | undefined {
	return packages?.find((candidate) => candidate.boards.includes(boardId));
}

/** True when `boardId` needs a package that is not installed (false while the list is unknown). */
export function needsPackage(packages: BoardPackage[] | null, boardId: string): boolean {
	const found = packageOf(packages, boardId);
	return found !== undefined && !found.shipped && found.installedVersion === "";
}

function boardNames(found: BoardPackage): string {
	return found.boards.map((id) => findBoard(id)?.name ?? id).join(", ");
}

function errorText(error: unknown): string {
	return error instanceof Error ? error.message : String(error);
}

interface Message {
	ok: boolean;
	text: string;
	details?: string;
}

/** Installs or removes a package; resolves to what to tell the user. */
async function change(found: BoardPackage, install: boolean, language: Language, t: Translate): Promise<Message> {
	try {
		const outcome = install
			? await bridge.boardPackageInstall(found.id, language)
			: await bridge.boardPackageRemove(found.id, language);
		if (outcome.ok) {
			return { ok: true, text: t(install ? "packages.installed_done" : "packages.removed_done", { name: found.name }) };
		}
		return { ok: false, text: outcome.message, details: outcome.details };
	} catch (error) {
		return { ok: false, text: errorText(error) };
	}
}

function MessageAlert({ message }: { message: Message }) {
	return (
		<Alert color={message.ok ? "green" : "red"} variant="light" p="xs">
			<Text size="sm" style={{ whiteSpace: "pre-line" }}>
				{message.text}
			</Text>
			{message.details && (
				<Code block mt={6} style={{ maxHeight: 120, overflow: "auto" }}>
					{message.details}
				</Code>
			)}
		</Alert>
	);
}

interface BoardPackageListProps {
	language: Language;
	t: Translate;
	/** Null while the list is read (or outside the application). */
	packages: BoardPackage[] | null;
	/** Something was installed or removed: read the list again. */
	onChanged(): Promise<void>;
}

/** The packages with their state and sizes, and the buttons to install, update or remove them. */
export function BoardPackageList({ language, t, packages, onChanged }: BoardPackageListProps) {
	/** Id of the package being installed or removed. */
	const [working, setWorking] = useState<string | null>(null);
	const [message, setMessage] = useState<Message | null>(null);

	const run = async (found: BoardPackage, install: boolean) => {
		if (!install && !window.confirm(t("packages.confirm_remove", { name: found.name, size: formatSize(found.diskBytes, language) }))) {
			return;
		}
		setWorking(found.id);
		setMessage(null);
		setMessage(await change(found, install, language, t));
		setWorking(null);
		await onChanged();
	};

	if (packages === null) {
		return (
			<Group gap="xs">
				<Loader size="xs" />
				<Text size="sm">{t("packages.loading")}</Text>
			</Group>
		);
	}
	return (
		<Stack gap="xs">
			{packages.map((found) => {
				const installed = found.installedVersion !== "";
				const busy = working === found.id;
				return (
					<Card key={found.id} withBorder padding="xs" data-testid={`package-${found.id}`}>
						<Group justify="space-between" wrap="nowrap" align="flex-start">
							<Stack gap={2} style={{ minWidth: 0 }}>
								<Group gap={6}>
									<Text fw={600} size="sm">
										{found.name}
									</Text>
									{installed && !found.shipped && (
										<Badge size="xs" color="green" variant="light">
											{t("packages.badge_installed")}
										</Badge>
									)}
								</Group>
								<Text size="xs" c="dimmed">
									{boardNames(found)}
								</Text>
								<Text size="xs">
									{found.shipped
										? t("packages.shipped")
										: installed
											? t("packages.installed", { version: found.installedVersion, size: formatSize(found.diskBytes, language) })
											: t("packages.available", {
													download: formatSize(found.downloadBytes, language),
													disk: formatSize(found.diskBytes, language),
												})}
								</Text>
								{installed && found.downloadBytes > 0 && (
									<Text size="xs" c="blue">
										{t("packages.update_available", { version: found.version, size: formatSize(found.downloadBytes, language) })}
									</Text>
								)}
								{busy && (
									<Group gap="xs">
										<Loader size="xs" />
										<Text size="xs">{t(installed ? "packages.removing" : "packages.installing")}</Text>
									</Group>
								)}
							</Stack>
							{!found.shipped && (
								<Group gap={6} wrap="nowrap">
									{installed && found.downloadBytes > 0 && (
										<Button size="xs" variant="light" disabled={working !== null} onClick={() => void run(found, true)}>
											{t("packages.update")}
										</Button>
									)}
									{installed ? (
										<Button size="xs" variant="default" disabled={working !== null} onClick={() => void run(found, false)}>
											{t("packages.remove")}
										</Button>
									) : (
										<Button size="xs" disabled={working !== null} onClick={() => void run(found, true)}>
											{t("packages.install")}
										</Button>
									)}
								</Group>
							)}
						</Group>
					</Card>
				);
			})}
			{message && <MessageAlert message={message} />}
		</Stack>
	);
}

interface BoardPackageDialogProps {
	language: Language;
	t: Translate;
	/** The board that needs a package (null: closed). */
	boardId: string | null;
	packages: BoardPackage[] | null;
	/** Installed: read the list again, then go on (Check, Upload or Live asked before). */
	onInstalled(): Promise<void>;
	onClose(): void;
}

/** Offers to install the package of a board, with its size; the board stays chosen either way. */
export function BoardPackageDialog({ language, t, boardId, packages, onInstalled, onClose }: BoardPackageDialogProps) {
	const [working, setWorking] = useState(false);
	const [message, setMessage] = useState<Message | null>(null);
	// Closes by itself once the package is installed (the list read again says so).
	const found = boardId && needsPackage(packages, boardId) ? packageOf(packages, boardId) : undefined;

	const close = () => {
		if (working) return;
		setMessage(null);
		onClose();
	};
	const install = async () => {
		if (!found) return;
		setWorking(true);
		setMessage(null);
		const outcome = await change(found, true, language, t);
		setWorking(false);
		if (outcome.ok) {
			await onInstalled();
		} else {
			setMessage(outcome);
		}
	};

	return (
		<Modal
			opened={found !== undefined}
			onClose={close}
			title={t("packages.ask_title")}
			centered
			closeOnClickOutside={!working}
			closeOnEscape={!working}
			withCloseButton={!working}
		>
			{found && (
				<Stack gap="sm">
					<Text size="sm">
						{t("packages.ask", {
							board: findBoard(boardId ?? "")?.name ?? boardId ?? "",
							name: found.name,
							download: formatSize(found.downloadBytes, language),
							disk: formatSize(found.diskBytes, language),
						})}
					</Text>
					<Text size="xs" c="dimmed">
						{t("packages.ask_sim")}
					</Text>
					{working && (
						<Group gap="xs">
							<Loader size="xs" />
							<Text size="sm">{t("packages.installing")}</Text>
						</Group>
					)}
					{message && <MessageAlert message={message} />}
					<Group justify="flex-end" gap="sm">
						<Button variant="default" disabled={working} onClick={close}>
							{t("packages.ask_later")}
						</Button>
						<Button loading={working} onClick={() => void install()}>
							{t("packages.install")}
						</Button>
					</Group>
				</Stack>
			)}
		</Modal>
	);
}
