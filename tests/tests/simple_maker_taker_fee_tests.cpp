#include <string>
#include <boost/test/unit_test.hpp>
#include <fc/exception/exception.hpp>

#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/market_object.hpp>

#include "../common/database_fixture.hpp"


using namespace graphene::chain;
using namespace graphene::chain::test;

struct simple_maker_taker_database_fixture : database_fixture {
   simple_maker_taker_database_fixture()
           : database_fixture() {
   }

   const limit_order_create_operation
   create_sell_operation(account_id_type user, const asset &amount, const asset &recv) {
      const time_point_sec order_expiration = time_point_sec::maximum();
      const price &fee_core_exchange_rate = price::unit_price();
      limit_order_create_operation op = create_sell_operation(user, amount, recv, order_expiration,
                                                              fee_core_exchange_rate);
      return op;
   }

   const limit_order_create_operation
   create_sell_operation(account_id_type user, const asset &amount, const asset &recv,
                         const time_point_sec order_expiration,
                         const price &fee_core_exchange_rate) {
      limit_order_create_operation op = create_sell_operation(user(db), amount, recv, order_expiration,
                                                              fee_core_exchange_rate);
      return op;
   }

   const limit_order_create_operation
   create_sell_operation(const account_object &user, const asset &amount, const asset &recv,
                         const time_point_sec order_expiration,
                         const price &fee_core_exchange_rate) {
      limit_order_create_operation sell_order;
      sell_order.seller = user.id;
      sell_order.amount_to_sell = amount;
      sell_order.min_to_receive = recv;
      sell_order.expiration = order_expiration;

      return sell_order;
   }
};


/**
 * BSIP81: Asset owners may specify different market fee rate for maker orders and taker orders
 */
BOOST_FIXTURE_TEST_SUITE(simple_maker_taker_fee_tests, simple_maker_taker_database_fixture)

   /**
    * Test of setting taker fee before HF and after HF for a UIA
    */
   BOOST_AUTO_TEST_CASE(setting_taker_fees_uia) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy));
         account_id_type issuer_id = jill.id;
         fc::ecc::private_key issuer_private_key = jill_private_key;

         upgrade_to_lifetime_member(izzy);

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));
         uint16_t market_fee_percent = 20 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                market_fee_percent);

         //////
         // Before HF, test inability to set taker fees
         //////
         asset_update_operation uop;
         uop.issuer = issuer_id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options = jillcoin.options;
         uint16_t new_taker_fee_percent = uop.new_options.market_fee_percent / 2;
         uop.new_options.taker_fee_percent = new_taker_fee_percent;

         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?

         // Check the taker fee
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test default values of taker fee after HF
         // After the HF its default value should be the market fee percent
         // which is effectively the new maker fee percent
         //////
         updated_asset = jillcoin.get_id()(db);
         expected_taker_fee_percent = updated_asset.options.market_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // After HF, test invalid taker fees
         //////
         uop.new_options.taker_fee_percent = GRAPHENE_100_PERCENT + 1;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?


         //////
         // After HF, test that new values can be set
         //////
         uop.new_options.taker_fee_percent = new_taker_fee_percent;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee
         updated_asset = jillcoin.get_id()(db);
         expected_taker_fee_percent = new_taker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of setting taker fee before HF and after HF for a smart asset
    */
   BOOST_AUTO_TEST_CASE(setting_taker_fees_smart_asset) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         const asset_object &bitsmart = create_bitasset("SMARTBIT", smartissuer.id);


         generate_blocks(HARDFORK_615_TIME); // get around Graphene issue #615 feed expiration bug
         generate_block();

         //////
         // Before HF, test inability to set taker fees
         //////
         asset_update_operation uop;
         uop.issuer = smartissuer.id;
         uop.asset_to_update = bitsmart.get_id();
         uop.new_options = bitsmart.options;
         uint16_t new_taker_fee_percent = uop.new_options.market_fee_percent / 2;
         uop.new_options.taker_fee_percent = new_taker_fee_percent;

         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?

         // Check the taker fee
         asset_object updated_asset = bitsmart.get_id()(db);
         uint16_t expected_taker_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test default values of taker fee after HF
         // After the HF its default value should be the market fee percent
         // which is effectively the new maker fee percent
         //////
         updated_asset = bitsmart.get_id()(db);
         expected_taker_fee_percent = updated_asset.options.market_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // After HF, test invalid taker fees
         //////
         uop.new_options.taker_fee_percent = GRAPHENE_100_PERCENT + 1;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?


         //////
         // After HF, test that new values can be set
         //////
         uop.new_options.taker_fee_percent = new_taker_fee_percent;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee
         updated_asset = bitsmart.get_id()(db);
         expected_taker_fee_percent = new_taker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test the default taker fee values of multiple different assets after HF
    */
   BOOST_AUTO_TEST_CASE(default_taker_fees) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob)(charlie)(smartissuer));

         // Initialize tokens with custom market fees
         price price(asset(1, asset_id_type(1)), asset(1));

         const uint16_t alice1coin_market_fee_percent = 1 * GRAPHENE_1_PERCENT;
         const asset_object alice1coin = create_user_issued_asset("ALICE1COIN", alice, charge_market_fee, price, 2,
                                                                  alice1coin_market_fee_percent);

         const uint16_t alice2coin_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object alice2coin = create_user_issued_asset("ALICE2COIN", alice, charge_market_fee, price, 2,
                                                                  alice2coin_market_fee_percent);

         const uint16_t bob1coin_market_fee_percent = 3 * GRAPHENE_1_PERCENT;
         const asset_object bob1coin = create_user_issued_asset("BOB1COIN", alice, charge_market_fee, price, 2,
                                                                bob1coin_market_fee_percent);

         const uint16_t bob2coin_market_fee_percent = 4 * GRAPHENE_1_PERCENT;
         const asset_object bob2coin = create_user_issued_asset("BOB2COIN", alice, charge_market_fee, price, 2,
                                                                bob2coin_market_fee_percent);

         const uint16_t charlie1coin_market_fee_percent = 4 * GRAPHENE_1_PERCENT;
         const asset_object charlie1coin = create_user_issued_asset("CHARLIE1COIN", alice, charge_market_fee, price, 2,
                                                                    charlie1coin_market_fee_percent);

         const uint16_t charlie2coin_market_fee_percent = 5 * GRAPHENE_1_PERCENT;
         const asset_object charlie2coin = create_user_issued_asset("CHARLIE2COIN", alice, charge_market_fee, price, 2,
                                                                    charlie2coin_market_fee_percent);

         const uint16_t bitsmart1coin_market_fee_percent = 7 * GRAPHENE_1_PERCENT;
         create_bitasset("SMARTBIT1", smartissuer.id, bitsmart1coin_market_fee_percent);
         generate_blocks(1); // The smart asset's ID will be updated after a block is generated
         const asset_object &bitsmart1 = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("SMARTBIT1");

         const uint16_t bitsmart2coin_market_fee_percent = 8 * GRAPHENE_1_PERCENT;
         create_bitasset("SMARTBIT2", smartissuer.id, bitsmart2coin_market_fee_percent);
         generate_blocks(1); // The smart asset's ID will be updated after a block is generated
         const asset_object &bitsmart2 = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("SMARTBIT2");


         //////
         // Before HF, test the market/maker fees for each asset
         //////
         asset_object updated_asset;
         uint16_t expected_fee_percent;

         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = alice1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = alice2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = bob1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = bob2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = charlie1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = charlie2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = bitsmart1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = bitsmart2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);


         //////
         // Before HF, test that taker fees are set to zero
         //////
         // Check the taker fee
         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test the maker fees for each asset are unchanged
         //////
         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = alice1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = alice2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = bob1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = bob2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = charlie1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = charlie2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = bitsmart1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = bitsmart2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);


         //////
         // After HF, test the taker fees for each asset are set, by default, to the maker fees
         //////
         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = alice1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = alice2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = bob1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = bob2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = charlie1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = charlie2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = bitsmart1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = bitsmart2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.taker_fee_percent);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of different maker and taker fees charged when filling limit orders after HF for a UIA
    */
   BOOST_AUTO_TEST_CASE(simple_match_and_fill_with_different_fees_uia) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy)(alice)(bob));

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));

         const uint16_t JILL_PRECISION = 100;
         const uint16_t jill_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                jill_market_fee_percent);

         const uint16_t IZZY_PRECISION = 1000;
         const uint16_t izzy_market_fee_percent = 5 * GRAPHENE_1_PERCENT;
         const asset_object izzycoin = create_user_issued_asset("ICOIN", izzy, charge_market_fee, price, 3,
                                                                izzy_market_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test that new values can be set
         //////
         // Define the new taker fees
         uint16_t jill_maker_fee_percent = jill_market_fee_percent;
         uint16_t jill_taker_fee_percent = jill_maker_fee_percent / 2;

         uint16_t izzy_maker_fee_percent = izzy_market_fee_percent;
         uint16_t izzy_taker_fee_percent = izzy_maker_fee_percent / 2;

         // Set the new taker fee for JILLCOIN
         asset_update_operation uop;
         uop.issuer = jill.id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options = jillcoin.options;
         uop.new_options.taker_fee_percent = jill_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, jill_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for JILLCOIN
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = jill_taker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);

         // Set the new taker fee for IZZYCOIN
         uop.issuer = izzy.id;
         uop.asset_to_update = izzycoin.get_id();
         uop.new_options = izzycoin.options;
         uop.new_options.taker_fee_percent = izzy_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, izzy_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for IZZYCOIN
         updated_asset = izzycoin.get_id()(db);
         expected_taker_fee_percent = izzy_taker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // After HF, create limit orders that will perfectly match
         //////
         BOOST_TEST_MESSAGE("Issuing 10 jillcoin to alice");
         issue_uia(alice, jillcoin.amount(10 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking alice's balance");
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 10 * JILL_PRECISION);

         BOOST_TEST_MESSAGE("Issuing 300 izzycoin to bob");
         issue_uia(bob, izzycoin.amount(300 * IZZY_PRECISION));
         BOOST_TEST_MESSAGE("Checking bob's balance");
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 300 * IZZY_PRECISION);

         // Alice and Bob place orders which match, and are completely filled by each other
         // Alice is willing to sell 10 JILLCOIN for at least 300 IZZYCOIN
         limit_order_create_operation alice_sell_op = create_sell_operation(alice.id,
                                                                            jillcoin.amount(10 * JILL_PRECISION),
                                                                            izzycoin.amount(300 *
                                                                                            IZZY_PRECISION));
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         const limit_order_object* alice_order_before = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order_before != nullptr);

         // Bob is willing to sell 300 IZZYCOIN for at least 10 JILLCOIN
         limit_order_create_operation bob_sell_op = create_sell_operation(bob.id, izzycoin.amount(300 * IZZY_PRECISION),
                                                                          jillcoin.amount(
                                                                                  10 *
                                                                                  JILL_PRECISION));
         trx.clear();
         trx.operations.push_back(bob_sell_op);
         asset bob_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, bob_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type bob_order_id = ptx.operation_results[0].get<object_id_type>();

         // Check that the orders were filled by ensuring that they are no longer on the order books
         const limit_order_object* alice_order = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order == nullptr);
         const limit_order_object* bob_order = db.find<limit_order_object>(bob_order_id);
         BOOST_CHECK(bob_order == nullptr);


         // Check the new balances of the maker
         // Alice was the maker; she is receiving IZZYCOIN
         asset expected_izzy_fee = jillcoin.amount(
                 300 * IZZY_PRECISION * izzy_maker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(alice, izzycoin),
                             (300 * IZZY_PRECISION) - alice_sell_fee.amount.value - expected_izzy_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 0);

         // Check the new balance of the taker
         // Bob was the taker; he is receiving JILLCOIN
         asset expected_jill_fee = jillcoin.amount(
                 10 * JILL_PRECISION * jill_taker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin),
                             (10 * JILL_PRECISION) - bob_sell_fee.amount.value - expected_jill_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 0);

         // Check the asset issuer's accumulated fees
         BOOST_CHECK(izzycoin.dynamic_asset_data_id(db).accumulated_fees == expected_izzy_fee.amount);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee.amount);

      } FC_LOG_AND_RETHROW()
   }


BOOST_AUTO_TEST_SUITE_END()