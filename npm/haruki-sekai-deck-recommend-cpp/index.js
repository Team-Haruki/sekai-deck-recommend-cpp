import createSekaiDeckRecommendModule from "./sekai_deck_recommend.js"

const DEFAULT_STATIC_DATA_PATH = "/data"

function isRecord(value) {
  return typeof value === "object" && value !== null && !Array.isArray(value)
}

function normalizeUserDataAliases(options) {
  if (!isRecord(options)) return options

  const normalized = { ...options }
  if (normalized.user_data === undefined && normalized.userData !== undefined) {
    normalized.user_data = normalized.userData
  }

  if (normalized.user_data_str === undefined && normalized.userDataStr !== undefined) {
    normalized.user_data_str = normalized.userDataStr
  }

  if (normalized.user_data_file_path === undefined && normalized.userDataFilePath !== undefined) {
    normalized.user_data_file_path = normalized.userDataFilePath
  }

  delete normalized.userData
  delete normalized.userDataStr
  delete normalized.userDataFilePath
  return normalized
}

function stringifyMusicMetas(value) {
  return typeof value === "string" ? value : JSON.stringify(value)
}

function createLocateFile({ locateFile, wasmUrl }) {
  if (!locateFile && !wasmUrl) return undefined

  return (path, prefix) => {
    if (path.endsWith(".wasm") && wasmUrl) return wasmUrl
    return locateFile ? locateFile(path, prefix) : `${prefix}${path}`
  }
}

export class SekaiDeckRecommendWasm {
  #module
  #engine
  #disposed = false

  constructor(module) {
    this.#module = module
    this.#engine = new module.SekaiDeckRecommend()
  }

  get module() {
    return this.#module
  }

  loadMasterData(region, data) {
    this.#assertActive()
    this.#engine.updateMasterdataFromObject(data, region)
  }

  loadMusicMetas(region, data) {
    this.#assertActive()
    this.#engine.updateMusicmetasFromString(stringifyMusicMetas(data), region)
  }

  recommend(options) {
    this.#assertActive()
    const json = this.#engine.recommend(JSON.stringify(normalizeUserDataAliases(options)))
    return JSON.parse(json)
  }

  getWorldBloomSupportCards(options) {
    this.#assertActive()
    const json = this.#engine.getWorldBloomSupportCards(JSON.stringify(normalizeUserDataAliases(options)))
    return JSON.parse(json)
  }

  dispose() {
    if (this.#disposed) return
    this.#engine.delete()
    this.#disposed = true
  }

  #assertActive() {
    if (this.#disposed) {
      throw new Error("SekaiDeckRecommendWasm has been disposed.")
    }
  }
}

export async function createSekaiDeckRecommend(options = {}) {
  const moduleOptions = {
    ...(options.moduleOptions ?? {}),
  }

  const locateFile = createLocateFile(options)
  if (locateFile && moduleOptions.locateFile === undefined) {
    moduleOptions.locateFile = locateFile
  }

  const module = await createSekaiDeckRecommendModule(moduleOptions)
  module.initDataPath(options.staticDataPath ?? DEFAULT_STATIC_DATA_PATH)
  return new SekaiDeckRecommendWasm(module)
}

export default createSekaiDeckRecommend

export { createSekaiDeckRecommendModule }
