# Feedback relay

Cloudflare Worker between the plugin's Feedback form and Discord. The plugin only knows this Worker's URL (embedded encrypted at build time); the Discord webhook URL lives solely in the Worker's `DISCORD_WEBHOOK` secret, so it can't be pulled out of the plugin. The Worker also checks a shared key and rate-limits each IP to 3 posts a minute and everyone to 200 a day (Workers KV counter), strips `@everyone`/`@here` and user pings, and caps message sizes.

## Setup

```powershell
cd relay
npm install
npx wrangler login                          # once, opens the browser
npx wrangler kv namespace create FEEDBACK_COUNTS   # on a new account: put the printed id in wrangler.toml
npx wrangler deploy                         # prints https://instant-clip-feedback.<you>.workers.dev
npx wrangler secret put DISCORD_WEBHOOK     # paste the webhook URL when asked
npx wrangler secret put RELAY_KEY           # paste the contents of ..\secrets\feedback-relay-key.txt
```

Then put `https://instant-clip-feedback.<you>.workers.dev/feedback` in `..\secrets\feedback-relay-url.txt` and rebuild the plugin.

To change the Discord channel later, run `npx wrangler secret put DISCORD_WEBHOOK` again; no plugin release needed.
