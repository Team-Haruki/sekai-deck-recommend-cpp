import { copyFile, stat } from "node:fs/promises"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"

const scriptDir = dirname(fileURLToPath(import.meta.url))
const packageDir = join(scriptDir, "..")
const repoRoot = join(packageDir, "..", "..")
const buildDir = join(repoRoot, "build_wasm")

const artifacts = [
  "sekai_deck_recommend.js",
  "sekai_deck_recommend.wasm",
]

for (const artifact of artifacts) {
  const source = join(buildDir, artifact)
  const destination = join(packageDir, artifact)

  try {
    await stat(source)
  } catch {
    throw new Error(
      `Missing ${source}. Build the wasm target before packing this npm package.`,
    )
  }

  await copyFile(source, destination)
  console.log(`Copied ${artifact}`)
}
