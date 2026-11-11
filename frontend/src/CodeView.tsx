// The code view: the code made from the blocks (read-only, always up to date) and the user's own
// "My code" file, whose functions and variables become blocks. In expert mode, the whole program
// is typed as text instead.
import { ActionIcon, Button, Card, Group, Select, Stack, Tabs, Text, Tooltip } from "@mantine/core";
import { IconTrash } from "@tabler/icons-react";

import type { Language } from "@/catalog";
import { CodeEditor } from "@/CodeEditor";
import type { MessageKey } from "@/i18n";
import { isScript, programLanguages, programNames, type CodeProblem } from "@/program";
import type { ProgramLanguage, UserCode } from "@/project/convert";

export type CodeTab = "blocks" | "mine";
type TextLanguage = Exclude<ProgramLanguage, "blocks">;

interface CodeViewProps {
	t(key: MessageKey, params?: Record<string, string>): string;
	language: Language;
	/** Changes when a project is opened: the editors start again from its code. */
	editorKey: number;
	tab: CodeTab;
	onTab(tab: CodeTab): void;
	/** Expert mode when not "blocks": the whole program is the typed code. */
	program: ProgramLanguage;
	onProgram(program: ProgramLanguage): void;
	programCode: string;
	onProgramCode(code: string): void;
	programProblems: CodeProblem[];
	/** C++ made from the blocks. */
	generated: string;
	userCode: UserCode | null;
	onUserCode(code: string): void;
	/** Creates "My code" in a language, switches it to another, or deletes it (null). */
	onUserLanguage(language: TextLanguage | null): void;
	userProblems: CodeProblem[];
	disabled?: boolean;
}

const textLanguages: TextLanguage[] = ["cpp", "python", "javascript"];

export function CodeView({
	t,
	language,
	editorKey,
	tab,
	onTab,
	program,
	onProgram,
	programCode,
	onProgramCode,
	programProblems,
	generated,
	userCode,
	onUserCode,
	onUserLanguage,
	userProblems,
	disabled,
}: CodeViewProps) {
	const expert = (
		<Select
			aria-label={t("program.expert")}
			size="xs"
			w={240}
			disabled={disabled}
			data={programLanguages.map((candidate) => ({
				value: candidate,
				label:
					candidate === "blocks"
						? t("program.expert_blocks")
						: `${t("program.expert")} : ${t("program.expert_text", { language: programNames[candidate] })}`,
			}))}
			value={program}
			onChange={(value) => {
				if (value) onProgram(value as ProgramLanguage);
			}}
			allowDeselect={false}
		/>
	);

	let content;
	if (program !== "blocks") {
		content = (
			<CodeEditor
				key={`${editorKey}-program-${program}`}
				program={program}
				language={language}
				initialCode={programCode}
				onChange={onProgramCode}
				problems={programProblems}
			/>
		);
	} else if (tab === "blocks") {
		content = (
			<Stack gap={0} h="100%">
				<Text size="sm" c="dimmed" px="md" py={6}>
					{t("code.blocks_readonly")}
				</Text>
				<div style={{ flex: 1, minHeight: 0 }}>
					<CodeEditor program="cpp" language={language} initialCode={generated} readOnly />
				</div>
			</Stack>
		);
	} else if (!userCode) {
		content = (
			<Card withBorder m="md" maw={640}>
				<Text mb="md">{t("code.mine_none")}</Text>
				<Group>
					{textLanguages.map((candidate) => (
						<Button key={candidate} variant="light" disabled={disabled} onClick={() => onUserLanguage(candidate)}>
							{programNames[candidate]}
						</Button>
					))}
				</Group>
				<Text size="sm" c="dimmed" mt="md">
					{t("code.mine_help_cpp")}
					<br />
					{t("code.mine_help_script", { language: "Python / JavaScript" })}
				</Text>
			</Card>
		);
	} else {
		content = (
			<Stack gap={0} h="100%">
				<Text size="sm" c="dimmed" px="md" py={6}>
					{isScript(userCode.language)
						? t("code.mine_help_script", { language: programNames[userCode.language] })
						: t("code.mine_help_cpp")}
				</Text>
				<div style={{ flex: 1, minHeight: 0 }}>
					<CodeEditor
						key={`${editorKey}-mine-${userCode.language}`}
						program={userCode.language}
						language={language}
						initialCode={userCode.code}
						onChange={onUserCode}
						problems={userProblems}
					/>
				</div>
			</Stack>
		);
	}

	return (
		<Stack gap={0} h="100%">
			<Group px="md" py={6} justify="space-between" wrap="wrap" style={{ borderBottom: "1px solid var(--mantine-color-gray-3)" }}>
				{program === "blocks" ? (
					<Tabs value={tab} onChange={(value) => onTab((value as CodeTab | null) ?? "blocks")}>
						<Tabs.List>
							<Tabs.Tab value="blocks">{t("code.tab_blocks")}</Tabs.Tab>
							<Tabs.Tab value="mine">
								{t("code.tab_mine")}
								{userCode ? ` (${programNames[userCode.language]})` : ""}
							</Tabs.Tab>
						</Tabs.List>
					</Tabs>
				) : (
					<Text fw={600}>{t("program.expert_text", { language: programNames[program] })}</Text>
				)}
				<Group gap="xs">
					{program === "blocks" && tab === "mine" && userCode && (
						<>
							<Select
								aria-label={t("code.mine_language")}
								size="xs"
								w={130}
								disabled={disabled}
								data={textLanguages.map((candidate) => ({ value: candidate, label: programNames[candidate] }))}
								value={userCode.language}
								onChange={(value) => {
									if (value) onUserLanguage(value as TextLanguage);
								}}
								allowDeselect={false}
							/>
							<Tooltip label={t("code.mine_remove")}>
								<ActionIcon
									variant="default"
									aria-label={t("code.mine_remove")}
									disabled={disabled}
									onClick={() => onUserLanguage(null)}
								>
									<IconTrash size={16} />
								</ActionIcon>
							</Tooltip>
						</>
					)}
					{expert}
				</Group>
			</Group>
			<div style={{ flex: 1, minHeight: 0, position: "relative" }}>{content}</div>
		</Stack>
	);
}
