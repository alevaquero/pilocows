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
