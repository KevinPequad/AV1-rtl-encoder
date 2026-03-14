#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { spawn, spawnSync } = require("child_process");

const SCRIPT_DIR = __dirname;
const DEFAULT_INITIAL_PROMPT = path.join(SCRIPT_DIR, "codex_supervisor.initial.md");
const DEFAULT_CONTINUE_PROMPT = path.join(SCRIPT_DIR, "codex_supervisor.continue.md");

function usage() {
    console.log(`Usage: node scripts/codex_supervisor.js [options]

Options:
  --repo <path>                 Repository root to supervise
  --state-dir <path>            Supervisor runtime directory inside the repo
  --model <name>                Codex model to pin on every turn
  --reasoning-effort <level>    Reasoning effort override
  --sandbox <mode>              Codex sandbox mode
  --sleep-seconds <n>           Delay between turns
  --max-runs <n>                Stop after n turns (0 = infinite)
  --initial-prompt-file <path>  Initial prompt template
  --continue-prompt-file <path> Continue prompt template
  --codex-bin <path>            Codex executable
  --fresh                       Start a new session instead of resuming
  --search                      Enable Codex web search on the first turn
  --stop-on-blocker             Exit if the agent reports status=blocked
  --skip-git-repo-check         Pass through to codex exec
  --help                        Show this help
`);
}

function fail(message) {
    console.error(`[supervisor] ${message}`);
    process.exit(1);
}

function parseIntFlag(value, name) {
    const parsed = Number.parseInt(value, 10);
    if (!Number.isFinite(parsed)) {
        fail(`invalid integer for ${name}: ${value}`);
    }
    return parsed;
}

function parseArgs(argv) {
    const options = {
        repo: process.cwd(),
        stateDir: ".codex-supervisor",
        model: "gpt-5.3-codex",
        reasoningEffort: "xhigh",
        sandbox: "danger-full-access",
        sleepSeconds: 10,
        maxRuns: 0,
        initialPromptFile: DEFAULT_INITIAL_PROMPT,
        continuePromptFile: DEFAULT_CONTINUE_PROMPT,
        codexBin: "codex",
        fresh: false,
        search: false,
        stopOnBlocker: false,
        skipGitRepoCheck: false,
    };

    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i];
        switch (arg) {
            case "--repo":
                options.repo = argv[++i];
                break;
            case "--state-dir":
                options.stateDir = argv[++i];
                break;
            case "--model":
                options.model = argv[++i];
                break;
            case "--reasoning-effort":
                options.reasoningEffort = argv[++i];
                break;
            case "--sandbox":
                options.sandbox = argv[++i];
                break;
            case "--sleep-seconds":
                options.sleepSeconds = parseIntFlag(argv[++i], "--sleep-seconds");
                break;
            case "--max-runs":
                options.maxRuns = parseIntFlag(argv[++i], "--max-runs");
                break;
            case "--initial-prompt-file":
                options.initialPromptFile = argv[++i];
                break;
            case "--continue-prompt-file":
                options.continuePromptFile = argv[++i];
                break;
            case "--codex-bin":
                options.codexBin = argv[++i];
                break;
            case "--fresh":
                options.fresh = true;
                break;
            case "--search":
                options.search = true;
                break;
            case "--stop-on-blocker":
                options.stopOnBlocker = true;
                break;
            case "--skip-git-repo-check":
                options.skipGitRepoCheck = true;
                break;
            case "--help":
            case "-h":
                usage();
                process.exit(0);
                break;
            default:
                fail(`unknown argument: ${arg}`);
        }
    }

    return options;
}

function resolveFile(base, candidate) {
    if (path.isAbsolute(candidate)) {
        return candidate;
    }
    return path.resolve(base, candidate);
}

function readText(filePath) {
    return fs.readFileSync(filePath, "utf8").trim();
}

function nowIso() {
    return new Date().toISOString();
}

function sleep(ms) {
    return new Promise((resolve) => {
        setTimeout(resolve, ms);
    });
}

function ensureDir(dirPath) {
    fs.mkdirSync(dirPath, { recursive: true });
}

function readJson(filePath) {
    if (!fs.existsSync(filePath)) {
        return null;
    }
    try {
        return JSON.parse(fs.readFileSync(filePath, "utf8"));
    } catch (error) {
        console.error(`[supervisor] failed to parse JSON from ${filePath}: ${error.message}`);
        return null;
    }
}

function writeJson(filePath, data) {
    const tempPath = `${filePath}.tmp`;
    fs.writeFileSync(tempPath, JSON.stringify(data, null, 2) + "\n", "utf8");
    fs.renameSync(tempPath, filePath);
}

function toRepoRelative(repoRoot, filePath) {
    return path.relative(repoRoot, filePath).split(path.sep).join("/");
}

function extractStatus(statusPath) {
    const raw = readJson(statusPath);
    if (!raw || typeof raw !== "object") {
        return null;
    }
    return {
        status: typeof raw.status === "string" ? raw.status : null,
        summary: typeof raw.summary === "string" ? raw.summary : null,
        nextPrompt: typeof raw.next_prompt === "string" ? raw.next_prompt : null,
        blocker: typeof raw.blocker === "string" ? raw.blocker : null,
        lastVerified: typeof raw.last_verified === "string" ? raw.last_verified : null,
        needsPush: Boolean(raw.needs_push),
        sleepSeconds: Number.isFinite(raw.sleep_seconds) ? raw.sleep_seconds : null,
    };
}

function buildInitialPrompt(options) {
    const base = readText(options.initialPromptFile);
    return [
        base,
        "",
        `Supervisor status file: ${options.statusPathRelative}`,
        `Supervisor runtime file: ${options.runtimePathRelative} (read-only for the agent)`,
    ].join("\n");
}

function buildContinuePrompt(options, runtime, status) {
    const base = readText(options.continuePromptFile);
    const lines = [
        base,
        "",
        `Supervisor status file: ${options.statusPathRelative}`,
        `Supervisor runtime file: ${options.runtimePathRelative} (read-only for the agent)`,
        `Recorded run count so far: ${runtime.runsCompleted || 0}`,
    ];

    if (status && status.status) {
        lines.push(`Last reported status: ${status.status}`);
    }
    if (status && status.summary) {
        lines.push(`Last summary: ${status.summary}`);
    }
    if (status && status.lastVerified) {
        lines.push(`Last verified checkpoint: ${status.lastVerified}`);
    }
    if (status && status.blocker) {
        lines.push(`Last reported blocker: ${status.blocker}`);
    }
    if (status && status.needsPush) {
        lines.push("The previous turn reported that a verified commit still needs to be pushed.");
    }
    if (status && status.nextPrompt) {
        lines.push("");
        lines.push("Carry-over next prompt:");
        lines.push(status.nextPrompt);
    } else {
        lines.push("");
        lines.push("Fallback continuation:");
        lines.push("Continue from the current highest-priority unfinished work item and keep going.");
    }

    return lines.join("\n");
}

function ensureCodexAvailable(codexBin) {
    const result = spawnSync(codexBin, ["--version"], { encoding: "utf8" });
    if (result.error) {
        fail(`unable to launch ${codexBin}: ${result.error.message}`);
    }
    if (result.status !== 0) {
        fail(`${codexBin} --version failed with exit code ${result.status}`);
    }
}

function pipeLines(stream, onLine) {
    let buffer = "";
    stream.setEncoding("utf8");
    stream.on("data", (chunk) => {
        buffer += chunk;
        while (true) {
            const newlineIndex = buffer.indexOf("\n");
            if (newlineIndex === -1) {
                break;
            }
            const line = buffer.slice(0, newlineIndex).replace(/\r$/, "");
            buffer = buffer.slice(newlineIndex + 1);
            onLine(line);
        }
    });
    stream.on("end", () => {
        if (buffer.length > 0) {
            onLine(buffer.replace(/\r$/, ""));
        }
    });
}

function buildCodexArgs(options, runtime, lastMessagePath) {
    const args = ["exec"];
    if (runtime.sessionId) {
        args.push("resume", runtime.sessionId);
    }

    args.push("-m", options.model);
    args.push("-c", `model_reasoning_effort="${options.reasoningEffort}"`);
    args.push("-c", 'approval_policy="never"');

    if (runtime.sessionId) {
        args.push("-c", `sandbox_mode="${options.sandbox}"`);
    } else {
        args.push("--sandbox", options.sandbox);
        if (options.search) {
            args.push("--search");
        }
        if (options.skipGitRepoCheck) {
            args.push("--skip-git-repo-check");
        }
        args.push("--color", "never");
    }

    args.push("--json", "--output-last-message", lastMessagePath, "-");
    return args;
}

async function runTurn(options, runtime, promptText) {
    const runNumber = (runtime.runsCompleted || 0) + 1;
    const runLabel = `run-${String(runNumber).padStart(4, "0")}`;
    const promptPath = path.join(options.statePath, `${runLabel}.prompt.txt`);
    const eventsPath = path.join(options.statePath, `${runLabel}.events.log`);
    const lastMessagePath = path.join(options.statePath, `${runLabel}.last_message.txt`);

    fs.writeFileSync(promptPath, promptText + "\n", "utf8");

    const args = buildCodexArgs(options, runtime, lastMessagePath);
    console.log(`[supervisor] starting ${runLabel} using ${runtime.sessionId ? "session " + runtime.sessionId : "a fresh session"}`);

    const logStream = fs.createWriteStream(eventsPath, { flags: "a", encoding: "utf8" });
    const child = spawn(options.codexBin, args, {
        cwd: options.repo,
        env: process.env,
        stdio: ["pipe", "pipe", "pipe"],
    });

    let sessionId = runtime.sessionId || null;

    const handleLine = (line) => {
        console.log(line);
        logStream.write(line + "\n");
        try {
            const event = JSON.parse(line);
            if (event && event.type === "thread.started" && typeof event.thread_id === "string") {
                sessionId = event.thread_id;
            }
        } catch (error) {
            return;
        }
    };

    pipeLines(child.stdout, handleLine);
    pipeLines(child.stderr, handleLine);

    child.stdin.write(promptText);
    child.stdin.end();

    const exitCode = await new Promise((resolve, reject) => {
        child.on("error", reject);
        child.on("close", resolve);
    }).catch((error) => {
        logStream.end();
        throw error;
    });

    logStream.end();

    return {
        exitCode,
        runNumber,
        runLabel,
        sessionId,
        promptPath,
        eventsPath,
        lastMessagePath,
    };
}

async function main() {
    const options = parseArgs(process.argv.slice(2));

    options.repo = path.resolve(options.repo);
    options.statePath = path.resolve(options.repo, options.stateDir);
    options.initialPromptFile = resolveFile(process.cwd(), options.initialPromptFile);
    options.continuePromptFile = resolveFile(process.cwd(), options.continuePromptFile);
    options.runtimePath = path.join(options.statePath, "runtime.json");
    options.statusPath = path.join(options.statePath, "status.json");
    options.statusPathRelative = toRepoRelative(options.repo, options.statusPath);
    options.runtimePathRelative = toRepoRelative(options.repo, options.runtimePath);

    if (!fs.existsSync(options.repo) || !fs.statSync(options.repo).isDirectory()) {
        fail(`repo path does not exist or is not a directory: ${options.repo}`);
    }
    if (!fs.existsSync(options.initialPromptFile)) {
        fail(`missing initial prompt file: ${options.initialPromptFile}`);
    }
    if (!fs.existsSync(options.continuePromptFile)) {
        fail(`missing continue prompt file: ${options.continuePromptFile}`);
    }

    ensureDir(options.statePath);
    ensureCodexAvailable(options.codexBin);

    let runtime = options.fresh ? null : readJson(options.runtimePath);
    if (!runtime || typeof runtime !== "object") {
        runtime = {};
    }

    runtime.sessionId = options.fresh ? null : runtime.sessionId || null;
    runtime.model = options.model;
    runtime.reasoningEffort = options.reasoningEffort;
    runtime.sandbox = options.sandbox;
    runtime.repo = options.repo;
    runtime.startedAt = runtime.startedAt || nowIso();
    runtime.updatedAt = nowIso();
    runtime.runsCompleted = Number.isFinite(runtime.runsCompleted) ? runtime.runsCompleted : 0;

    writeJson(options.runtimePath, runtime);

    while (true) {
        if (options.maxRuns > 0 && runtime.runsCompleted >= options.maxRuns) {
            console.log(`[supervisor] reached max runs (${options.maxRuns}), stopping`);
            return;
        }

        const statusBeforeTurn = extractStatus(options.statusPath);
        const promptText = runtime.sessionId
            ? buildContinuePrompt(options, runtime, statusBeforeTurn)
            : buildInitialPrompt(options);

        const turnStartedAt = nowIso();
        runtime.updatedAt = turnStartedAt;
        writeJson(options.runtimePath, runtime);

        let result;
        try {
            result = await runTurn(options, runtime, promptText);
        } catch (error) {
            runtime.lastExitCode = -1;
            runtime.lastError = error.message;
            runtime.updatedAt = nowIso();
            writeJson(options.runtimePath, runtime);
            console.error(`[supervisor] codex launch failed: ${error.message}`);
            await sleep(options.sleepSeconds * 1000);
            continue;
        }

        runtime.sessionId = result.sessionId || runtime.sessionId || null;
        runtime.runsCompleted = result.runNumber;
        runtime.lastRunLabel = result.runLabel;
        runtime.lastPromptPath = toRepoRelative(options.repo, result.promptPath);
        runtime.lastEventsPath = toRepoRelative(options.repo, result.eventsPath);
        runtime.lastMessagePath = toRepoRelative(options.repo, result.lastMessagePath);
        runtime.lastTurnStartedAt = turnStartedAt;
        runtime.lastTurnFinishedAt = nowIso();
        runtime.lastExitCode = result.exitCode;
        runtime.updatedAt = runtime.lastTurnFinishedAt;
        delete runtime.lastError;

        const statusAfterTurn = extractStatus(options.statusPath);
        if (statusAfterTurn) {
            runtime.lastReportedStatus = statusAfterTurn.status;
            runtime.lastSummary = statusAfterTurn.summary;
            runtime.lastVerified = statusAfterTurn.lastVerified;
            runtime.lastBlocker = statusAfterTurn.blocker;
        }

        writeJson(options.runtimePath, runtime);

        if (result.exitCode !== 0) {
            console.error(`[supervisor] codex exited with ${result.exitCode}; retrying in ${options.sleepSeconds}s`);
            await sleep(options.sleepSeconds * 1000);
            continue;
        }

        if (statusAfterTurn && statusAfterTurn.status === "complete") {
            console.log("[supervisor] agent reported completion");
            return;
        }

        if (statusAfterTurn && statusAfterTurn.status === "blocked" && options.stopOnBlocker) {
            console.error("[supervisor] agent reported a hard blocker and --stop-on-blocker is set");
            process.exit(2);
        }

        const delaySeconds = statusAfterTurn && Number.isFinite(statusAfterTurn.sleepSeconds)
            ? statusAfterTurn.sleepSeconds
            : options.sleepSeconds;

        console.log(`[supervisor] next turn in ${delaySeconds}s`);
        await sleep(delaySeconds * 1000);
    }
}

main().catch((error) => {
    console.error(`[supervisor] fatal error: ${error.stack || error.message}`);
    process.exit(1);
});
