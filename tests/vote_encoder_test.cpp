#include <gtest/gtest.h>
#include <brightchain/vote_encoder.hpp>
#include <brightchain/paillier.hpp>

using namespace brightchain;

class VoteEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate test keys (512-bit for faster tests)
        auto keyPair = deriveVotingKeysFromECDH(
            std::vector<uint8_t>(32, 0x01),  // Test private key
            std::vector<uint8_t>(33, 0x02),  // Test public key
            512,  // Small key for testing
            16    // Fewer iterations for testing
        );
        publicKey_ = keyPair.publicKey;
        privateKey_ = keyPair.privateKey;
        encoder_ = std::make_unique<VoteEncoder>(publicKey_);
    }

    std::vector<uint8_t> intToBytes(int64_t value) {
        std::vector<uint8_t> result;
        if (value == 0) {
            result.push_back(0);
            return result;
        }
        
        bool negative = value < 0;
        uint64_t absValue = negative ? -value : value;
        
        while (absValue > 0) {
            result.push_back(static_cast<uint8_t>(absValue & 0xFF));
            absValue >>= 8;
        }
        
        if (negative) {
            result.push_back(0xFF);
        }
        
        return result;
    }

    int64_t bytesToInt(const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) return 0;
        
        int64_t result = 0;
        for (size_t i = 0; i < bytes.size() && i < 8; i++) {
            result |= (static_cast<int64_t>(bytes[i]) << (i * 8));
        }
        return result;
    }

    std::shared_ptr<PaillierPublicKey> publicKey_;
    std::shared_ptr<PaillierPrivateKey> privateKey_;
    std::unique_ptr<VoteEncoder> encoder_;
};

TEST_F(VoteEncoderTest, PluralityEncoding_EncryptsVoteForSelectedChoice) {
    auto vote = encoder_->encodePlurality(1, 3);

    EXPECT_TRUE(vote.choiceIndex.has_value());
    EXPECT_EQ(vote.choiceIndex.value(), 1);
    EXPECT_EQ(vote.encrypted.size(), 3);
}

TEST_F(VoteEncoderTest, PluralityEncoding_DecryptsToCorrectValues) {
    auto vote = encoder_->encodePlurality(1, 3);

    auto decrypted0 = privateKey_->decrypt(vote.encrypted[0]);
    auto decrypted1 = privateKey_->decrypt(vote.encrypted[1]);
    auto decrypted2 = privateKey_->decrypt(vote.encrypted[2]);

    EXPECT_EQ(bytesToInt(decrypted0), 0);
    EXPECT_EQ(bytesToInt(decrypted1), 1);
    EXPECT_EQ(bytesToInt(decrypted2), 0);
}

TEST_F(VoteEncoderTest, PluralityEncoding_ProducesDifferentCiphertexts) {
    auto vote1 = encoder_->encodePlurality(0, 2);
    auto vote2 = encoder_->encodePlurality(0, 2);

    EXPECT_NE(vote1.encrypted[0], vote2.encrypted[0]);
}

TEST_F(VoteEncoderTest, ApprovalEncoding_EncryptsMultipleApprovals) {
    auto vote = encoder_->encodeApproval({0, 2}, 4);

    EXPECT_TRUE(vote.choices.has_value());
    EXPECT_EQ(vote.choices.value(), std::vector<int>({0, 2}));
    EXPECT_EQ(vote.encrypted.size(), 4);
}

TEST_F(VoteEncoderTest, ApprovalEncoding_DecryptsToCorrectApprovals) {
    auto vote = encoder_->encodeApproval({0, 2}, 4);

    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 0);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[3])), 0);
}

TEST_F(VoteEncoderTest, ApprovalEncoding_HandlesEmptyApprovalSet) {
    auto vote = encoder_->encodeApproval({}, 3);

    for (const auto& enc : vote.encrypted) {
        EXPECT_EQ(bytesToInt(privateKey_->decrypt(enc)), 0);
    }
}

TEST_F(VoteEncoderTest, WeightedEncoding_EncryptsWeightForChoice) {
    auto weight = intToBytes(500);
    auto vote = encoder_->encodeWeighted(1, weight, 3);

    EXPECT_TRUE(vote.choiceIndex.has_value());
    EXPECT_EQ(vote.choiceIndex.value(), 1);
    EXPECT_TRUE(vote.weight.has_value());
    EXPECT_EQ(vote.encrypted.size(), 3);
}

TEST_F(VoteEncoderTest, WeightedEncoding_DecryptsToCorrectWeight) {
    auto weight = intToBytes(500);
    auto vote = encoder_->encodeWeighted(1, weight, 3);

    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 0);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 500);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 0);
}

TEST_F(VoteEncoderTest, BordaEncoding_AssignsPointsByRanking) {
    auto vote = encoder_->encodeBorda({2, 0, 1}, 3);

    EXPECT_TRUE(vote.rankings.has_value());
    EXPECT_EQ(vote.rankings.value(), std::vector<int>({2, 0, 1}));
    EXPECT_EQ(vote.encrypted.size(), 3);
}

TEST_F(VoteEncoderTest, BordaEncoding_DecryptsToCorrectPoints) {
    auto vote = encoder_->encodeBorda({2, 0, 1}, 3);

    // First choice (index 2) gets 3 points
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 3);
    // Second choice (index 0) gets 2 points
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 2);
    // Third choice (index 1) gets 1 point
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 1);
}

TEST_F(VoteEncoderTest, BordaEncoding_HandlesPartialRankings) {
    auto vote = encoder_->encodeBorda({1, 0}, 4);

    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 2);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 0);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[3])), 0);
}

TEST_F(VoteEncoderTest, RankedChoiceEncoding_StoresRankingPositions) {
    auto vote = encoder_->encodeRankedChoice({1, 2, 0}, 3);

    EXPECT_TRUE(vote.rankings.has_value());
    EXPECT_EQ(vote.rankings.value(), std::vector<int>({1, 2, 0}));
    EXPECT_EQ(vote.encrypted.size(), 3);
}

TEST_F(VoteEncoderTest, RankedChoiceEncoding_DecryptsToRankingPositions) {
    auto vote = encoder_->encodeRankedChoice({1, 2, 0}, 3);

    // Index 1 is first choice (rank 1)
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 1);
    // Index 2 is second choice (rank 2)
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 2);
    // Index 0 is third choice (rank 3)
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 3);
}

TEST_F(VoteEncoderTest, RankedChoiceEncoding_HandlesPartialRankings) {
    auto vote = encoder_->encodeRankedChoice({2, 0}, 4);

    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 2);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 0);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[3])), 0);
}

TEST_F(VoteEncoderTest, GenericEncode_EncodesPlurality) {
    auto vote = encoder_->encode(VotingMethod::Plurality, 1, std::nullopt, std::nullopt, std::nullopt, 3);

    EXPECT_TRUE(vote.choiceIndex.has_value());
    EXPECT_EQ(vote.choiceIndex.value(), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[1])), 1);
}

TEST_F(VoteEncoderTest, GenericEncode_EncodesApproval) {
    auto vote = encoder_->encode(VotingMethod::Approval, std::nullopt, std::vector<int>{0, 2}, std::nullopt, std::nullopt, 3);

    EXPECT_TRUE(vote.choices.has_value());
    EXPECT_EQ(vote.choices.value(), std::vector<int>({0, 2}));
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[2])), 1);
}

TEST_F(VoteEncoderTest, GenericEncode_EncodesWeighted) {
    auto weight = intToBytes(100);
    auto vote = encoder_->encode(VotingMethod::Weighted, 0, std::nullopt, std::nullopt, weight, 2);

    EXPECT_TRUE(vote.weight.has_value());
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 100);
}

TEST_F(VoteEncoderTest, GenericEncode_ThrowsForMissingData) {
    EXPECT_THROW(
        encoder_->encode(VotingMethod::Plurality, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 2),
        std::invalid_argument
    );

    EXPECT_THROW(
        encoder_->encode(VotingMethod::Approval, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 2),
        std::invalid_argument
    );

    EXPECT_THROW(
        encoder_->encode(VotingMethod::Weighted, 0, std::nullopt, std::nullopt, std::nullopt, 2),
        std::invalid_argument
    );

    EXPECT_THROW(
        encoder_->encode(VotingMethod::Borda, std::nullopt, std::nullopt, std::nullopt, std::nullopt, 2),
        std::invalid_argument
    );
}

TEST_F(VoteEncoderTest, EdgeCases_HandlesSingleChoice) {
    auto vote = encoder_->encodePlurality(0, 1);
    EXPECT_EQ(vote.encrypted.size(), 1);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 1);
}

TEST_F(VoteEncoderTest, EdgeCases_HandlesZeroWeight) {
    auto weight = intToBytes(0);
    auto vote = encoder_->encodeWeighted(0, weight, 2);
    EXPECT_EQ(bytesToInt(privateKey_->decrypt(vote.encrypted[0])), 0);
}
