import * as fs from "fs";
import * as path from "path";
import * as crypto from "crypto";
import { fileURLToPath } from "url";
import * as secrets from "@digitaldefiance/secrets";

const __filename = fileURLToPath(import.meta.url);
const outputDir = path.dirname(__filename);

// Helper to generate random hex string
function randomHex(bytes: number): string {
  return crypto.randomBytes(bytes).toString("hex");
}

function generateShamirVectors() {
  const vectors = [];

  // Test different bit lengths and scenarios
  const configs = [
    { bits: 8, testData: ["deadbeef", "0123456789abcdef", "00000001", "ffffffff", randomHex(16)] },
    { bits: 10, testData: ["cafebabe", "0102030405060708", "00000000", "ffffffff", randomHex(20)] },
    { bits: 12, testData: ["abcdef01", randomHex(24), "0000", "ffff", randomHex(32)] },
    { bits: 16, testData: ["deadbeefcafebabe", randomHex(32), "00000000", "ffffffff", randomHex(64)] },
  ];

  for (const config of configs) {
    for (const secret of config.testData) {
      // Try different share combinations
      const combinations = [
        { numShares: 5, threshold: 3 },
        { numShares: 10, threshold: 5 },
        { numShares: 3, threshold: 2 },
      ];

      for (const combo of combinations) {
        try {
          const bits = config.bits;
          if (bits < 3 || bits > 20) continue;

          secrets.init(bits);
          const shares = secrets.share(secret, combo.numShares, combo.threshold);

          vectors.push({
            bits,
            secret,
            shares: shares,
            threshold: combo.threshold,
            shareCount: combo.numShares,
            description: `${bits}-bit Shamir (${secret.length * 4} bits), ${combo.numShares}/${combo.threshold} threshold`,
          });
        } catch (e) {
          console.warn(`Failed Shamir config (${config.bits} bits, ${combo.numShares}/${combo.threshold}):`, e);
        }
      }
    }
  }

  return vectors;
}

async function main() {
  try {
    console.log("Generating comprehensive test vectors...\n");

    // Generate Shamir vectors only (ECIES will be tested via the existing generators)
    console.log("Generating Shamir vectors...");
    const shamirVectors = generateShamirVectors();
    console.log(`✓ Generated ${shamirVectors.length} Shamir test vectors`);

    const shamirOutput = { shamir: shamirVectors };
    fs.writeFileSync(
      path.join(outputDir, "test_vectors_comprehensive_shamir.json"),
      JSON.stringify(shamirOutput, null, 2)
    );

    console.log("\n✓ Comprehensive test vectors generated successfully!");
    console.log(`  Shamir: test_vectors_comprehensive_shamir.json`);
  } catch (error) {
    console.error("Error generating test vectors:", error);
    process.exit(1);
  }
}

main();
