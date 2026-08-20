-- Add optional father/mother lineage to animals. Both reference another
-- animal (by internal id, resolved from an EID at the API layer) and may be
-- NULL — parentage is frequently unknown for purchased or older stock.
-- ON DELETE SET NULL: removing a parent's animal record must not cascade
-- into deleting its offspring, just clear the now-dangling reference.

ALTER TABLE animals ADD COLUMN father_id INTEGER REFERENCES animals(id) ON DELETE SET NULL;
ALTER TABLE animals ADD COLUMN mother_id INTEGER REFERENCES animals(id) ON DELETE SET NULL;

CREATE INDEX IF NOT EXISTS idx_animals_father_id ON animals(father_id);
CREATE INDEX IF NOT EXISTS idx_animals_mother_id ON animals(mother_id);
