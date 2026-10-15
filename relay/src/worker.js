// Feedback relay for the Instant Clip OBS plugin.
//
// The plugin POSTs feedback here; this Worker checks it and forwards it to Discord.
// The Discord webhook URL only exists as the DISCORD_WEBHOOK secret on Cloudflare,
// so it never ships inside the plugin, the repo or a release.
//
// Secrets (set with `npx wrangler secret put <NAME>`):
//   DISCORD_WEBHOOK  the channel webhook URL
//   RELAY_KEY        shared key the plugin sends in X-Instant-Clip-Key
// Bindings (wrangler.toml): FEEDBACK_LIMITER, 3 requests per minute per IP;
// FEEDBACK_COUNTS (KV), the daily total.

const KINDS = {
	bug: { label: "Bug report", color: 0xf06e6e },
	idea: { label: "Idea", color: 0x6b8cff },
	other: { label: "Feedback", color: 0xa0a3b1 },
};

const LIMITS = { message: 2000, contact: 100, version: 40, os: 80 };

// Total messages forwarded per UTC day, across all senders. The per-IP limiter stops one
// sender; this stops many. KV is eventually consistent, so the cap is approximate.
const DAILY_LIMIT = 200;

async function overDailyLimit(env) {
	const key = "count:" + new Date().toISOString().slice(0, 10);
	const count = Number((await env.FEEDBACK_COUNTS.get(key)) || 0);
	if (count >= DAILY_LIMIT) return true;
	await env.FEEDBACK_COUNTS.put(key, String(count + 1), { expirationTtl: 2 * 24 * 60 * 60 });
	return false;
}

function json(body, status = 200) {
	return new Response(JSON.stringify(body), {
		status,
		headers: { "content-type": "application/json" },
	});
}

function keyMatches(given, expected) {
	if (!given || !expected) return false;
	const enc = new TextEncoder();
	const a = enc.encode(given);
	const b = enc.encode(expected);
	if (a.byteLength !== b.byteLength) return false;
	return crypto.subtle.timingSafeEqual(a, b);
}

// Plain text only: no pings, no markdown links that could hide where they point.
function clean(value, max) {
	if (typeof value !== "string") return "";
	return value
		.replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, "")
		.replace(/@(everyone|here)/gi, "@​$1")
		.replace(/<@[!&]?\d+>/g, "@user")
		.slice(0, max)
		.trim();
}

export default {
	async fetch(request, env) {
		const url = new URL(request.url);
		if (url.pathname !== "/feedback") return json({ error: "not_found" }, 404);
		if (request.method !== "POST") return json({ error: "method_not_allowed" }, 405);

		// trim(): secrets piped in from a shell can pick up a trailing newline.
		if (!keyMatches(request.headers.get("x-instant-clip-key"), (env.RELAY_KEY || "").trim())) {
			return json({ error: "forbidden" }, 403);
		}

		const ip = request.headers.get("cf-connecting-ip") || "unknown";
		const { success } = await env.FEEDBACK_LIMITER.limit({ key: ip });
		if (!success) return json({ error: "rate_limited" }, 429);

		if (Number(request.headers.get("content-length") || 0) > 16 * 1024) {
			return json({ error: "too_large" }, 413);
		}

		let data;
		try {
			data = await request.json();
		} catch {
			return json({ error: "bad_json" }, 400);
		}

		const kind = KINDS[data.kind] ? data.kind : "other";
		const message = clean(data.message, LIMITS.message);
		if (message.length < 3) return json({ error: "empty_message" }, 400);
		const contact = clean(data.contact, LIMITS.contact);

		if (await overDailyLimit(env)) return json({ error: "rate_limited" }, 429);

		const embed = {
			title: KINDS[kind].label,
			description: message,
			color: KINDS[kind].color,
			fields: [
				...(contact ? [{ name: "Contact", value: contact, inline: false }] : []),
				{ name: "Plugin", value: clean(data.plugin_version, LIMITS.version) || "?", inline: true },
				{ name: "OBS", value: clean(data.obs_version, LIMITS.version) || "?", inline: true },
				{ name: "Windows", value: clean(data.os, LIMITS.os) || "?", inline: true },
			],
			timestamp: new Date().toISOString(),
		};

		const upstream = await fetch(env.DISCORD_WEBHOOK, {
			method: "POST",
			headers: { "content-type": "application/json" },
			body: JSON.stringify({
				username: "Instant Clip feedback",
				embeds: [embed],
				allowed_mentions: { parse: [] },
			}),
		});
		if (!upstream.ok) return json({ error: "upstream", status: upstream.status }, 502);

		return json({ ok: true });
	},
};
