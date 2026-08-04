import createSekaiDeckRecommendModule from "./sekai_deck_recommend.js"

const DEFAULT_STATIC_DATA_PATH = "/data"
const USER_DATA_HANDLE_CONSTRUCTOR_KEY = Symbol("SekaiDeckRecommendUserData")
// Keep this projection in sync with UserData::loadFromJson in src/data-provider/user-data.cpp.
const USER_DATA_KEYS = [
  "userGamedata",
  "userAreas",
  "userCards",
  "userChallengeLiveSoloDecks",
  "userCharacters",
  "userDecks",
  "userHonors",
  "userMysekaiCanvases",
  "userMysekaiFixtureGameCharacterPerformanceBonuses",
  "userMysekaiGates",
]

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

function stripUserDataOptions(options) {
  const normalized = normalizeUserDataAliases(options)
  if (!isRecord(normalized)) return normalized

  const stripped = { ...normalized }
  delete stripped.user_data
  delete stripped.user_data_str
  delete stripped.user_data_file_path
  return stripped
}

function stringifyUserData(value) {
  if (typeof value === "string") return value

  let serializable = value
  if (isRecord(value)) {
    serializable = {}
    for (const key of USER_DATA_KEYS) {
      if (Object.prototype.hasOwnProperty.call(value, key)) {
        serializable[key] = value[key]
      }
    }
  }

  const json = JSON.stringify(serializable)
  if (typeof json !== "string") {
    throw new TypeError("createUserData expects a JSON-serializable user-data object or string.")
  }
  return json
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

export class SekaiDeckRecommendUserData {
  #raw
  #owner
  #onDispose

  constructor(key, raw, owner, onDispose) {
    if (key !== USER_DATA_HANDLE_CONSTRUCTOR_KEY) {
      throw new TypeError("User-data handles must be created by SekaiDeckRecommendWasm.createUserData().")
    }
    this.#raw = raw
    this.#owner = owner
    this.#onDispose = onDispose
    Object.freeze(this)
  }

  get disposed() {
    return this.#raw === undefined
  }

  dispose() {
    if (this.#raw === undefined) return
    this.#raw.delete()
    this.#raw = undefined
    const onDispose = this.#onDispose
    this.#onDispose = undefined
    onDispose?.()
  }

  _getRaw(owner) {
    if (this.#raw === undefined) {
      throw new Error("SekaiDeckRecommendUserData has been disposed.")
    }
    if (owner !== this.#owner) {
      throw new Error("SekaiDeckRecommendUserData belongs to a different engine.")
    }
    return this.#raw
  }
}

export class SekaiDeckRecommendWasm {
  #module
  #engine
  #userDataOwner = {}
  #userDataHandles = new Set()
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

  createUserData(region, data) {
    this.#assertActive()
    const raw = this.#engine.createUserData(stringifyUserData(data), region)
    let handle
    handle = new SekaiDeckRecommendUserData(
      USER_DATA_HANDLE_CONSTRUCTOR_KEY,
      raw,
      this.#userDataOwner,
      () => this.#userDataHandles.delete(handle),
    )
    this.#userDataHandles.add(handle)
    return handle
  }

  recommend(options, userData) {
    this.#assertActive()
    const normalized = userData === undefined
      ? normalizeUserDataAliases(options)
      : stripUserDataOptions(options)
    const optionsJson = JSON.stringify(normalized)
    const json = userData === undefined
      ? this.#engine.recommend(optionsJson)
      : this.#engine.recommendWithUserData(optionsJson, this.#getRawUserData(userData))
    return JSON.parse(json)
  }

  recommendBatch(optionsList, userData) {
    this.#assertActive()
    if (!Array.isArray(optionsList)) {
      throw new TypeError("recommendBatch expects an array of options.")
    }
    const normalized = userData === undefined
      ? optionsList.map(normalizeUserDataAliases)
      : optionsList.map(stripUserDataOptions)
    const optionsJson = JSON.stringify(normalized)
    const json = userData === undefined
      ? this.#engine.recommendBatch(optionsJson)
      : this.#engine.recommendBatchWithUserData(optionsJson, this.#getRawUserData(userData))
    return JSON.parse(json)
  }

  getWorldBloomSupportCards(options) {
    this.#assertActive()
    const json = this.#engine.getWorldBloomSupportCards(JSON.stringify(normalizeUserDataAliases(options)))
    return JSON.parse(json)
  }

  recommendAreaItems(options) {
    this.#assertActive()
    const json = this.#engine.recommendAreaItems(JSON.stringify(normalizeUserDataAliases(options)))
    return JSON.parse(json)
  }

  recommendMusic(options, deck) {
    this.#assertActive()
    const json = this.#engine.recommendMusic(JSON.stringify({ ...options, deck }))
    return JSON.parse(json)
  }

  calculateExactLive(options) {
    this.#assertActive()
    const json = this.#engine.calculateExactLive(JSON.stringify(options))
    return JSON.parse(json)
  }

  dispose() {
    if (this.#disposed) return
    for (const handle of [...this.#userDataHandles]) handle.dispose()
    this.#engine.delete()
    this.#disposed = true
  }

  #getRawUserData(userData) {
    if (!(userData instanceof SekaiDeckRecommendUserData)) {
      throw new TypeError("userData must be created by this engine's createUserData().")
    }
    return userData._getRaw(this.#userDataOwner)
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
