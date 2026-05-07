use serde::{Deserialize, Serialize};
use sqlx::FromRow;

// ─── Tags ────────────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct Tag {
    pub id: i64,
    pub tag_number: String,
    pub purchased_at: String,
    pub notes: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateTag {
    pub tag_number: String,
    pub purchased_at: String,
    #[serde(default)]
    pub notes: String,
}

// ─── Animals ─────────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct Animal {
    pub id: i64,
    pub tag_id: i64,
    pub name: String,
    pub breed: String,
    pub category: String,
    pub sex: String,
    pub dob: Option<String>,
    pub notes: String,
    pub is_active: i64,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Serialize, FromRow)]
pub struct AnimalWithTag {
    pub id: i64,
    pub tag_id: i64,
    pub tag_number: String,
    pub name: String,
    pub breed: String,
    pub category: String,
    pub sex: String,
    pub dob: Option<String>,
    pub notes: String,
    pub is_active: i64,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateAnimal {
    pub tag_id: i64,
    pub name: String,
    pub breed: String,
    pub category: String,
    pub sex: String,
    pub dob: Option<String>,
    #[serde(default)]
    pub notes: String,
}

#[derive(Debug, Deserialize, Default)]
pub struct PatchAnimal {
    pub name: Option<String>,
    pub breed: Option<String>,
    pub category: Option<String>,
    pub sex: Option<String>,
    pub dob: Option<String>,
    pub notes: Option<String>,
    pub is_active: Option<bool>,
}

// ─── Vaccinations ─────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct Vaccination {
    pub id: i64,
    pub animal_id: i64,
    pub vaccine: String,
    pub dose: String,
    pub administered_at: String,
    pub next_due_at: Option<String>,
    pub notes: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateVaccination {
    pub vaccine: String,
    #[serde(default)]
    pub dose: String,
    pub administered_at: String,
    pub next_due_at: Option<String>,
    #[serde(default)]
    pub notes: String,
}

// ─── Pregnancies ──────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct Pregnancy {
    pub id: i64,
    pub animal_id: i64,
    pub result: String,
    pub checked_at: String,
    pub due_date: Option<String>,
    pub notes: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreatePregnancy {
    pub result: String,
    pub checked_at: String,
    pub due_date: Option<String>,
    #[serde(default)]
    pub notes: String,
}

// ─── TB Tests ─────────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct TbTest {
    pub id: i64,
    pub animal_id: i64,
    pub result: String,
    pub tested_at: String,
    pub notes: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateTbTest {
    pub result: String,
    pub tested_at: String,
    #[serde(default)]
    pub notes: String,
}

// ─── Weights ──────────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct Weight {
    pub id: i64,
    pub animal_id: i64,
    pub weight_kg: f64,
    pub weighed_at: String,
    pub notes: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateWeight {
    pub weight_kg: f64,
    pub weighed_at: String,
    #[serde(default)]
    pub notes: String,
}

// ─── Removals ─────────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct Removal {
    pub id: i64,
    pub animal_id: i64,
    pub reason: String,
    pub removed_at: String,
    pub notes: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateRemoval {
    pub reason: String,
    pub removed_at: String,
    #[serde(default)]
    pub notes: String,
}

// ─── Health record patches ───────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
pub struct PatchVaccination {
    pub vaccine: Option<String>,
    pub dose: Option<String>,
    pub administered_at: Option<String>,
    pub next_due_at: Option<String>,
    pub notes: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct PatchPregnancy {
    pub result: Option<String>,
    pub checked_at: Option<String>,
    pub due_date: Option<String>,
    pub notes: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct PatchTbTest {
    pub result: Option<String>,
    pub tested_at: Option<String>,
    pub notes: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct PatchWeight {
    pub weight_kg: Option<f64>,
    pub weighed_at: Option<String>,
    pub notes: Option<String>,
}

// ─── Scan Events ──────────────────────────────────────────────────────────────

#[derive(Debug, Serialize, FromRow)]
pub struct ScanEvent {
    pub id: i64,
    pub eid: String,
    pub event_type: String,
    pub scanned_at: String,
    pub weight_kg: Option<f64>,
    pub pregnancy_result: Option<String>,
    pub tb_result: Option<String>,
    pub vaccines: String,
    pub notes: String,
    pub synced_at: String,
    pub animal_id: Option<i64>,
}

#[derive(Debug, Deserialize)]
pub struct IncomingScan {
    pub eid: String,
    pub event_type: String,
    pub scanned_at: String,
    pub weight_kg: Option<f64>,
    pub pregnancy_result: Option<String>,
    pub tb_result: Option<String>,
    #[serde(default)]
    pub vaccines: String,
    #[serde(default)]
    pub notes: String,
}

// ─── Full Animal Profile ──────────────────────────────────────────────────────

#[derive(Debug, Serialize)]
pub struct AnimalProfile {
    #[serde(flatten)]
    pub animal: AnimalWithTag,
    pub vaccinations: Vec<Vaccination>,
    pub pregnancies: Vec<Pregnancy>,
    pub tb_tests: Vec<TbTest>,
    pub weights: Vec<Weight>,
    pub removal: Option<Removal>,
}

// ─── Query filters ───────────────────────────────────────────────────────────

#[derive(Debug, Deserialize, Default)]
pub struct AnimalQuery {
    pub tag_number: Option<String>,
    pub category: Option<String>,
    pub is_active: Option<bool>,
}

#[derive(Debug, Deserialize, Default)]
pub struct TagQuery {
    pub unassigned: Option<bool>,
}

// ─── Sessions ────────────────────────────────────────────────────────────────

/// Full session row from the DB (no records).
#[derive(Debug, Serialize, FromRow)]
pub struct Session {
    pub id: i64,
    pub handheld_session_id: i64,
    pub device_id: String,
    pub name: String,
    #[sqlx(rename = "type")]
    pub session_type: i64,
    pub status: i64,
    pub created_at: String,
    pub tag_count: i64,
    pub handheld_note: String,
    pub farm: String,
    pub operator: String,
    pub comments: String,
    pub synced_at: String,
}

/// Session row + computed record_count for list responses.
#[derive(Debug, Serialize, FromRow)]
pub struct SessionSummary {
    pub id: i64,
    pub handheld_session_id: i64,
    pub device_id: String,
    pub name: String,
    #[sqlx(rename = "type")]
    pub session_type: i64,
    pub status: i64,
    pub created_at: String,
    pub tag_count: i64,
    pub handheld_note: String,
    pub farm: String,
    pub operator: String,
    pub comments: String,
    pub synced_at: String,
    pub record_count: i64,
}

/// One tag-record row from the DB.
#[derive(Debug, Serialize, FromRow)]
pub struct SessionRecord {
    pub id: i64,
    pub session_id: i64,
    pub eid: String,
    pub scanned_at: String,
    pub event_data: String,  // JSON blob
    pub note: String,
    // Joined from tags/animals — NULL when EID is not registered.
    pub animal_id: Option<i64>,
    pub animal_name: Option<String>,
}

/// Full session detail: session metadata + all records.
#[derive(Debug, Serialize)]
pub struct SessionDetail {
    #[serde(flatten)]
    pub session: Session,
    pub records: Vec<SessionRecord>,
}

// ── Incoming sync payload ─────────────────────────────────────────────────────

/// Session metadata as sent by the frontend after reading SESSION_META from BLE.
#[derive(Debug, Deserialize)]
pub struct IncomingSession {
    pub handheld_session_id: i64,
    pub device_id: String,
    pub name: String,
    #[serde(rename = "type")]
    pub session_type: i64,
    pub status: i64,
    pub created_at: i64,    // Unix timestamp from handheld
    pub tag_count: i64,
    #[serde(default)]
    pub note: String,       // handheld session note
}

/// One tag record as sent by the frontend after reading SESSION_DATA pages from BLE.
#[derive(Debug, Deserialize)]
pub struct IncomingSessionRecord {
    pub eid: String,
    pub ts: i64,            // Unix timestamp from handheld
    #[serde(rename = "type")]
    pub session_type: i64,
    // Type-specific fields (only one will be present per record)
    pub weight_kg: Option<f64>,
    pub pregnancy: Option<String>,
    pub tb_result: Option<String>,
    pub vaccines: Option<String>,
    #[serde(default)]
    pub note: String,
}

/// Full sync body: one session + all its records.
#[derive(Debug, Deserialize)]
pub struct SyncSessionPayload {
    pub session: IncomingSession,
    pub records: Vec<IncomingSessionRecord>,
}

/// Response from POST /sessions/sync.
#[derive(Debug, Serialize)]
pub struct SyncSessionResponse {
    pub session_id: i64,   // DB id of the upserted session
    pub upserted_records: usize,
}

// ── Session patch ─────────────────────────────────────────────────────────────

/// Only frontend-editable fields; handheld_note can also be edited here.
#[derive(Debug, Deserialize, Default)]
pub struct PatchSession {
    pub farm: Option<String>,
    pub operator: Option<String>,
    pub comments: Option<String>,
    pub handheld_note: Option<String>,
}
