export type SekaiRegion = "jp" | "tw" | "en" | "kr" | "cn"

export type RecommendTarget = "score" | "skill" | "power" | "bonus"

export type RecommendAlgorithm = "sa" | "dfs" | "ga" | "dfs_ga" | "rl"

export type MusicDifficulty = "easy" | "normal" | "hard" | "expert" | "master" | "append"

export type LiveType =
  | "multi"
  | "solo"
  | "challenge"
  | "cheerful"
  | "auto"
  | "mysekai"
  | "challenge_auto"

export type EventAttr = "mysterious" | "cool" | "pure" | "cute" | "happy"

export type UnitType = "light_sound" | "idol" | "street" | "theme_park" | "school_refusal" | "piapro"

export type EventType = "marathon" | "cheerful_carnival" | "world_bloom"

export type SkillChooseStrategy = "average" | "max" | "min"

export type SkillOrderChooseStrategy = SkillChooseStrategy | "specific"

export interface WasmModuleOptions {
  locateFile?: (path: string, prefix: string) => string
  print?: (text: string) => void
  printErr?: (text: string) => void
  [key: string]: unknown
}

export interface CreateSekaiDeckRecommendOptions {
  wasmUrl?: string
  staticDataPath?: string
  locateFile?: (path: string, prefix: string) => string
  moduleOptions?: WasmModuleOptions
}

export interface CardConfig {
  disable?: boolean
  level_max?: boolean
  episode_read?: boolean
  master_max?: boolean
  skill_max?: boolean
  canvas?: boolean
  level?: number
  skill_level?: number
  master_rank?: number
  episode_read_count?: number
}

export interface SingleCardConfig extends CardConfig {
  card_id: number
}

export interface SaOptions {
  run_num?: number
  seed?: number
  max_iter?: number
  max_no_improve_iter?: number
  time_limit_ms?: number
  start_temprature?: number
  cooling_rate?: number
  debug?: boolean
}

export interface GaOptions {
  seed?: number
  debug?: boolean
  max_iter?: number
  max_no_improve_iter?: number
  pop_size?: number
  parent_size?: number
  elite_size?: number
  crossover_rate?: number
  base_mutation_rate?: number
  no_improve_iter_to_mutation_rate?: number
}

export interface RecommendOptions {
  region: SekaiRegion
  live_type: LiveType
  music_id: number
  music_diff: MusicDifficulty
  user_data?: unknown
  userData?: unknown
  user_data_str?: string
  userDataStr?: string
  user_data_file_path?: string
  userDataFilePath?: string
  event_id?: number
  world_bloom_event_turn?: number
  world_bloom_character_id?: number
  challenge_live_character_id?: number
  event_attr?: EventAttr
  event_unit?: UnitType
  event_type?: EventType
  target?: RecommendTarget
  algorithm?: RecommendAlgorithm
  target_bonus_list?: number[]
  custom_bonus_attr?: EventAttr
  custom_bonus_character_ids?: number[]
  custom_bonus_character_support_units?: Record<string, Exclude<UnitType, "piapro">>
  filter_other_unit?: boolean
  limit?: number
  member?: number
  fixed_cards?: number[]
  fixed_characters?: number[]
  forcedLeaderCharacterId?: number
  skill_reference_choose_strategy?: SkillChooseStrategy
  keep_after_training_state?: boolean
  multi_live_teammate_score_up?: number
  multi_live_teammate_power?: number
  best_skill_as_leader?: boolean
  multi_live_score_up_lower_bound?: number
  skill_order_choose_strategy?: SkillOrderChooseStrategy
  specific_skill_order?: number[]
  timeout_ms?: number
  rarity_1_config?: CardConfig
  rarity_2_config?: CardConfig
  rarity_3_config?: CardConfig
  rarity_birthday_config?: CardConfig
  rarity_4_config?: CardConfig
  single_card_configs?: SingleCardConfig[]
  support_master_max?: boolean
  support_skill_max?: boolean
  sa_options?: SaOptions
  ga_options?: GaOptions
  [key: string]: unknown
}

export interface WorldBloomSupportOptions {
  region: SekaiRegion
  user_data?: unknown
  userData?: unknown
  user_data_str?: string
  userDataStr?: string
  user_data_file_path?: string
  userDataFilePath?: string
  event_id?: number
  world_bloom_event_turn?: number
  world_bloom_character_id?: number
  forcedLeaderCharacterId?: number
  event_unit?: UnitType
  support_master_max?: boolean
  support_skill_max?: boolean
  [key: string]: unknown
}

export interface RecommendCard {
  card_id: number
  total_power: number
  base_power: number
  event_bonus_rate: number
  master_rank: number
  level: number
  skill_level: number
  skill_score_up: number
  skill_life_recovery: number
  episode1_read: boolean
  episode2_read: boolean
  after_training: boolean
  default_image: string
  has_canvas_bonus: boolean
}

export interface RecommendDeck {
  score: number
  live_score: number
  mysekai_event_point: number
  total_power: number
  base_power: number
  area_item_bonus_power: number
  character_bonus_power: number
  honor_bonus_power: number
  fixture_bonus_power: number
  gate_bonus_power: number
  event_bonus_rate: number
  support_deck_bonus_rate: number
  multi_live_score_up: number
  support_deck_cards?: WorldBloomSupportCard[]
  cards: RecommendCard[]
}

export interface RecommendResult {
  decks: RecommendDeck[]
}

export interface WorldBloomSupportCard {
  card_id: number
  bonus: number
  skill_level?: number
  master_rank?: number
  level?: number
  after_training?: boolean
  default_image?: string
}

export interface RawSekaiDeckRecommendInstance {
  updateMasterdataFromObject(data: Record<string, unknown>, region: SekaiRegion): void
  updateMusicmetasFromString(data: string, region: SekaiRegion): void
  recommend(optionsJson: string): string
  getWorldBloomSupportCards(optionsJson: string): string
  delete(): void
}

export interface RawSekaiDeckRecommendModule {
  SekaiDeckRecommend: new () => RawSekaiDeckRecommendInstance
  initDataPath(path: string): void
  [key: string]: unknown
}

export function createSekaiDeckRecommendModule(
  moduleOptions?: WasmModuleOptions,
): Promise<RawSekaiDeckRecommendModule>

export class SekaiDeckRecommendWasm {
  constructor(module: RawSekaiDeckRecommendModule)
  readonly module: RawSekaiDeckRecommendModule
  loadMasterData(region: SekaiRegion, data: Record<string, unknown>): void
  loadMusicMetas(region: SekaiRegion, data: string | object): void
  recommend(options: RecommendOptions): RecommendResult
  getWorldBloomSupportCards(options: WorldBloomSupportOptions): WorldBloomSupportCard[]
  dispose(): void
}

export function createSekaiDeckRecommend(
  options?: CreateSekaiDeckRecommendOptions,
): Promise<SekaiDeckRecommendWasm>

export default createSekaiDeckRecommend
