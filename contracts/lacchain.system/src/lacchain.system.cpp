#include <lacchain.system/lacchain.system.hpp>
#include <lacchain.system/safe.hpp>
//#include <eosiolib/core/datastream.hpp>
namespace lacchainsystem {

void lacchain::setabi( name account, const std::vector<char>& abi ) {
   abi_hash_table table(get_self(), get_self().value);
   auto itr = table.find( account.value );
   if( itr == table.end() ) {
      table.emplace( account, [&]( auto& row ) {
         row.owner = account;
         row.hash  = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
      });
   } else {
      table.modify( itr, eosio::same_payer, [&]( auto& row ) {
         row.hash = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
      });
   }
}

void lacchain::onerror( uint128_t sender_id, const std::vector<char>& sent_trx ) {
   eosio::check( false, "the onerror action cannot be called directly" );
}

void lacchain::setpriv( name account, uint8_t is_priv ) {
   require_auth( get_self() );
   set_privileged( account, is_priv );
}

void lacchain::setalimits( name account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight ) {
   require_auth( get_self() );
   set_resource_limits( account, ram_bytes, net_weight, cpu_weight );
}

void lacchain::setparams( const eosio::blockchain_parameters& params ) {
   require_auth( get_self() );
   set_blockchain_parameters( params );
}

void lacchain::reqauth( name from ) {
   require_auth( from );
}

void lacchain::activate( const eosio::checksum256& feature_digest ) {
   require_auth( get_self() );
   preactivate_feature( feature_digest );
}

void lacchain::reqactivated( const eosio::checksum256& feature_digest ) {
   eosio::check( is_feature_activated( feature_digest ), "protocol feature is not activated" );
}

void lacchain::newaccount( name creator, name name, const authority& owner, const authority& active ) {

   entity_table entities(get_self(), get_self().value);
   auto itr = entities.find( creator.value );

   if( itr == entities.end() ) {
      itr = entities.find( name.value );
      eosio::check(creator == get_self(), "Only the permissioning committee can create an entity account");
      eosio::check(itr != entities.end(), "Entity not found");

      if( itr->type == entity_type::VALIDATOR ) {
         //TODO: think how much
         set_resource_limits( name, 1*(1 << 20), 1 << 23, 1 << 23 );
      } else if ( itr->type == entity_type::WRITER ) {
         
         //TODO: default values, now 200MB + ~1/total_writers of cpu/net resources
         set_resource_limits( name, 200*(1 << 20), 1 << 30, 1 << 30 );
         
         //TODO: Use a new table with the writer@access permission configuration
         
         authority auth;
         auth.threshold = 1;
         
         itr = entities.begin();
         while( itr != entities.end() ) {
            if( itr->type == entity_type::WRITER ) {
               auth.accounts.push_back({
                  {itr->name, "active"_n},
                  1    
               });
            }
            ++itr;
         }
         
         updateauth_action(get_self(), {"writer"_n, "active"_n}).send( "writer"_n, "access"_n, "owner"_n, auth);

      } else if ( itr->type == entity_type::BOOT ) {
         //TODO: think how much
         set_resource_limits( name, 1*(1 << 20), 1 << 23, 1 << 23 );
      } else if ( itr->type == entity_type::OBSERVER ) {
         //TODO: think how much
         set_resource_limits( name, 1*(1 << 20), 1 << 23, 1 << 23 );
      } else {
         check(false, "Unknown entity type");
      }
      //eosio::check(itr->type == entity_type::VALIDATOR || itr->type == entity_type::WRITER, "Only validators and writers can have an accounts");
   } else {
      eosio::print(itr->type);
      eosio::check(itr->type == entity_type::WRITER, "Only writers entities can create new accounts");
      eosio::check(validate_newuser_authority(active), "invalid active authority");
      eosio::check(validate_newuser_authority(owner), "invalid owner authority");

      //0 CPU/NET for user accounts
      int64_t ram, cpu, net;
      get_resource_limits( name, ram, cpu, net);
      set_resource_limits( name, ram, 0, 0);
   }
}

void lacchain::add_new_entity(const name& entity_name,
                              const entity_type entity_type,
                              const authority& owner, const authority& active,
                              const std::optional<eosio::block_signing_authority> bsa,
                              const uint16_t location) {
   require_auth( get_self() );
   
   entity_table entities(get_self(), get_self().value);
   auto itr = entities.find( entity_name.value );
   
   eosio::check(itr == entities.end(), "An entity with the same name already exists");
   
   entities.emplace( get_self(), [&]( auto& e ) {
      e.name = entity_name;
      e.type = entity_type;
      e.location  = location;
      e.bsa  = bsa;
   });

   newaccount_action(get_self(), {get_self(), "active"_n}).send( get_self(), entity_name, owner, active );   
}

void lacchain::addvalidator( const name& validator,
                             const authority& owner,
                             const authority& active,
                             const eosio::block_signing_authority& validator_authority,
                             const uint16_t location ) {
   add_new_entity(validator, entity_type::VALIDATOR, owner, active, validator_authority, location);
}

void lacchain::addwriter( const name& writer, const authority& owner,
                          const authority& active, const uint16_t location ) {
   add_new_entity(writer, entity_type::WRITER, owner, active, {}, location);
}

void lacchain::addboot( const name& boot, const authority& owner,
                          const authority& active, const uint16_t location ) {
   add_new_entity(boot, entity_type::BOOT, owner, active, {}, location);
}

void lacchain::addobserver( const name& observer, const authority& owner,
                          const authority& active, const uint16_t location ) {
   //TODO: think => should observers have accounts? 
   add_new_entity(observer, entity_type::OBSERVER, owner, active, {}, location);
}

void lacchain::addnetlink( const name& entityA, const name& entityB, int direction ) {
   require_auth( get_self() );

   netlink_table netlinks( get_self(), get_self().value );
   auto index = netlinks.get_index<"pair"_n>();
   auto itr = index.find( netlink::make_key(entityA.value, entityB.value) );

   auto update_link_record = [&]( auto& l ) {
      l.entityA   = entityA;
      l.entityB   = entityB;
      l.direction = direction;
   };

   if( itr == index.end() ) {
      entity_table entities(get_self(), get_self().value);

      auto itr = entities.find( entityA.value );
      eosio::check(itr != entities.end(), "entity A not found");
      itr = entities.find( entityB.value );
      eosio::check(itr != entities.end(), "entity B not found");

      netlinks.emplace( get_self(), update_link_record);
   } else {
      netlinks.modify( *itr, eosio::same_payer, update_link_record);
   }
}

void lacchain::rmnetlink( const name& entityA, const name& entityB ) {
   require_auth( get_self() );

   netlink_table netlinks( get_self(), get_self().value );
   auto index = netlinks.get_index<"pair"_n>();
   auto itr = index.find( netlink::make_key(entityA.value, entityB.value) );
   eosio::check(itr != index.end(), "netlink not found");
   index.erase( itr );
}

void lacchain::setschedule( const std::vector<name>& validators ) {
   require_auth( get_self() );

   //TODO: change to vector<eosio::producer_authority>
   std::vector<eosio::producer_key> schedule;
   schedule.resize(validators.size());

   //TODO: use standard serialization
   char* buffer = (char*)malloc(1024);
   eosio::datastream<char*> ds(buffer, 1024);
   
   ds << eosio::unsigned_int(validators.size());

   entity_table entities(get_self(), get_self().value);
   for(const auto& v : validators) {
      auto itr = entities.find( v.value );
      eosio::check(itr != entities.end(), "Validator not found");
      eosio::check(!!itr->bsa, "Invalid block signing authority");
      schedule.push_back(eosio::producer_key{
         itr->name, std::get<eosio::block_signing_authority_v0>(*itr->bsa).keys[0].key
      });

      eosio::print("name =>", "[", itr->name ,"][", v.value, "][", itr->name.value, "]");
      ds << v.value;
      ds << std::get<eosio::block_signing_authority_v0>(*itr->bsa).keys[0].key;
   }
   
   //TODO: check serialization   
   eosio::check(schedule.size(), "Schedule cant be empty");
   
   eosio::print(" => [");
   eosio::printhex((char*)buffer, ds.tellp());
   eosio::print(" ]");
   //auto packed_prods = eosio::pack( schedule );
   eosio::internal_use_do_not_use::set_proposed_producers((char*)buffer, ds.tellp());
   //set_proposed_producers( schedule );
}

bool lacchain::validate_newuser_authority(const authority& auth) {

   safe<uint16_t> weight_sum_without_entity = 0;
   safe<uint16_t> entity_weight = 0;
   bool writer_in_authority  = false;

   for( const auto& k : auth.keys) {
      weight_sum_without_entity += safe<uint16_t>(k.weight);
   }

   for( const auto& plw : auth.accounts) {
      if(plw.permission.actor == "writer"_n) {
         eosio::check(plw.permission.permission == "access"_n, "only `access` permission is allowed");
         entity_weight = safe<uint16_t>(plw.weight);
         writer_in_authority = true;
      } else { 
         weight_sum_without_entity += safe<uint16_t>(plw.weight);
      }
   }

   return writer_in_authority && 
          weight_sum_without_entity < auth.threshold &&
          weight_sum_without_entity + entity_weight == auth.threshold;
}

} //lacchainsystem