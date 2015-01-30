#include <bts/blockchain/balance_operations.hpp>
#include <bts/blockchain/exceptions.hpp>
#include <bts/blockchain/pending_chain_state.hpp>

namespace bts { namespace blockchain {

   asset balance_record::calculate_yield( fc::time_point_sec now, share_type amount, share_type yield_pool, share_type share_supply )const
   {
      if( amount <= 0 )       return asset(0,condition.asset_id);
      if( share_supply <= 0 ) return asset(0,condition.asset_id);
      if( yield_pool <= 0 )   return asset(0,condition.asset_id);

      fc::uint128 current_supply( share_supply - yield_pool );
      if( current_supply == 0 )
         return asset(0, condition.asset_id);

      auto elapsed_time = (now - deposit_date);
      if( elapsed_time <= fc::seconds( BTS_BLOCKCHAIN_MIN_YIELD_PERIOD_SEC ) )
         return asset(0, condition.asset_id);
      
      fc::uint128 amount_withdrawn( amount );
      amount_withdrawn *= 1000000;

      fc::uint128 fee_fund( yield_pool );

      //
      // numerator in the following expression is at most
      // BTS_BLOCKCHAIN_MAX_SHARES * 1000000 * BTS_BLOCKCHAIN_MAX_SHARES
      // thus it cannot overflow
      //
      fc::uint128 yield = (amount_withdrawn * fee_fund) / current_supply;

      /**
       *  If less than 1 year, the 80% of yield are scaled linearly with time and 20% scaled by time^2,
       *  this should produce a yield curve that is "80% simple interest" and 20% simulating compound
       *  interest.
       */
      if( elapsed_time < fc::seconds( BTS_BLOCKCHAIN_MAX_YIELD_PERIOD_SEC ) )
      {
         fc::uint128 original_yield = yield;
         //
         // discount the yield by 80%
         //
         // yield largest value in following multiplication is at most
         // BTS_BLOCKCHAIN_MAX_SHARES * 1000000 * BTS_BLOCKCHAIN_MAX_SHARES * 8
         // thus it cannot overflow
         //
         yield *= 8;
         yield /= 10;

         fc::uint128 delta_yield = original_yield - yield;

         // yield == amount withdrawn / total usd  *  fee fund * fraction_of_year
         yield *= elapsed_time.to_seconds();
         yield /= BTS_BLOCKCHAIN_MAX_YIELD_PERIOD_SEC;

         delta_yield *= elapsed_time.to_seconds();
         delta_yield /= BTS_BLOCKCHAIN_MAX_YIELD_PERIOD_SEC;

         delta_yield *= elapsed_time.to_seconds();
         delta_yield /= BTS_BLOCKCHAIN_MAX_YIELD_PERIOD_SEC;

         yield += delta_yield;
      }

      yield /= 1000000;
      if( yield > 0 && yield < yield_pool )
      {
         uint64_t yield_amount = yield.to_uint64();
         return asset( yield_amount, condition.asset_id );
      }
      return asset( 0, condition.asset_id );
   }

   balance_id_type deposit_operation::balance_id()const
   {
      return condition.get_address();
   }

   deposit_operation::deposit_operation( const address& owner,
                                         const asset& amnt,
                                         slate_id_type slate_id )
   {
      FC_ASSERT( amnt.amount > 0 );
      amount = amnt.amount;
      condition = withdraw_condition( withdraw_with_signature( owner ),
                                      amnt.asset_id, slate_id );
   }

   void define_delegate_slate_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      if( this->slate.supported_delegates.size() > BTS_BLOCKCHAIN_MAX_SLATE_SIZE )
         FC_CAPTURE_AND_THROW( too_may_delegates_in_slate, (slate.supported_delegates.size()) );

      // verify slates are sorted
      FC_ASSERT( slate.supported_delegates.size() > 0 );
      for( uint32_t i = 1; i < slate.supported_delegates.size(); ++i )
      {
         FC_ASSERT( slate.supported_delegates[i-1] < slate.supported_delegates[i] );
      }

      const slate_id_type slate_id = this->slate.id();
      const odelegate_slate current_slate = eval_state._current_state->get_delegate_slate( slate_id );
      if( NOT current_slate.valid() )
      {
         eval_state._current_state->store_delegate_slate( slate_id, slate );
      }
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void deposit_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
       if( this->amount <= 0 )
          FC_CAPTURE_AND_THROW( negative_deposit, (amount) );

       switch( withdraw_condition_types( this->condition.type ) )
       {
          case withdraw_signature_type:
          case withdraw_vesting_type:
          case withdraw_multisig_type:
          case withdraw_escrow_type:
             break;
          default:
             FC_CAPTURE_AND_THROW( invalid_withdraw_condition, (*this) );
       }

       const balance_id_type deposit_balance_id = this->balance_id();

       obalance_record cur_record = eval_state._current_state->get_balance_record( deposit_balance_id );
       if( !cur_record.valid() )
       {
          cur_record = balance_record( this->condition );
          if( this->condition.type == withdraw_escrow_type )
             cur_record->meta_data = variant_object("creating_transaction_id", eval_state.trx.id() );
       }

       if( cur_record->balance == 0 )
       {
          cur_record->deposit_date = eval_state._current_state->now();
       }
       else
       {
          fc::uint128 old_sec_since_epoch( cur_record->deposit_date.sec_since_epoch() );
          fc::uint128 new_sec_since_epoch( eval_state._current_state->now().sec_since_epoch() );

          fc::uint128 avg = (old_sec_since_epoch * cur_record->balance) + (new_sec_since_epoch * this->amount);
          avg /= (cur_record->balance + this->amount);

          cur_record->deposit_date = time_point_sec( avg.to_integer() );
       }

       cur_record->balance += this->amount;
       eval_state.sub_balance( deposit_balance_id, asset( this->amount, cur_record->condition.asset_id ) );

       if( cur_record->condition.asset_id == 0 && cur_record->condition.slate_id )
          eval_state.adjust_vote( cur_record->condition.slate_id, this->amount );

       cur_record->last_update = eval_state._current_state->now();

       const oasset_record asset_rec = eval_state._current_state->get_asset_record( cur_record->condition.asset_id );
       FC_ASSERT( asset_rec.valid() );

       if( asset_rec->is_market_issued() ) FC_ASSERT( cur_record->condition.slate_id == 0 );

       if( asset_rec->is_restricted() )
       {
         for( const auto& owner : cur_record->owners() )
         {
           FC_ASSERT( eval_state._current_state->get_authorization(asset_rec->id, owner) );
         }
       }

       eval_state._current_state->store_balance_record( *cur_record );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void withdraw_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
       if( this->amount <= 0 )
          FC_CAPTURE_AND_THROW( negative_withdraw, (amount) );

      obalance_record current_balance_record = eval_state._current_state->get_balance_record( this->balance_id );
      if( !current_balance_record.valid() )
         FC_CAPTURE_AND_THROW( unknown_balance_record, (balance_id) );

      if( this->amount > current_balance_record->get_spendable_balance( eval_state._current_state->now() ).amount )
         FC_CAPTURE_AND_THROW( insufficient_funds, (current_balance_record)(amount) );

      auto asset_rec = eval_state._current_state->get_asset_record( current_balance_record->condition.asset_id );
      FC_ASSERT( asset_rec.valid() );
      bool issuer_override = asset_rec->is_retractable() && eval_state.verify_authority( asset_rec->authority );


      if( !issuer_override )
      {
         FC_ASSERT( !asset_rec->is_balance_frozen() );
         if( asset_rec->is_restricted() )
         {
            const auto& owner_addrs = current_balance_record->owners();
            for( const auto& owner : owner_addrs )
            {
              FC_ASSERT(eval_state._current_state->get_authorization(asset_rec->id, owner));
            }
         }
         switch( (withdraw_condition_types)current_balance_record->condition.type )
         {
            case withdraw_signature_type:
            {
                const withdraw_with_signature condition = current_balance_record->condition.as<withdraw_with_signature>();
                const address owner = condition.owner;
                if( !eval_state.check_signature( owner ) )
                    FC_CAPTURE_AND_THROW( missing_signature, (owner) );
                break;
            }

            case withdraw_vesting_type:
            {
                const withdraw_vesting condition = current_balance_record->condition.as<withdraw_vesting>();
                const address owner = condition.owner;
                if( !eval_state.check_signature( owner ) )
                    FC_CAPTURE_AND_THROW( missing_signature, (owner) );
                break;
            }

            case withdraw_multisig_type:
            {
               auto multisig = current_balance_record->condition.as<withdraw_with_multisig>();
               uint32_t valid_signatures = 0;
               for( const auto& sig : multisig.owners )
                  valid_signatures += eval_state.check_signature( sig );
               if( valid_signatures < multisig.required )
                  FC_CAPTURE_AND_THROW( missing_signature, (valid_signatures)(multisig) );
               break;
            }

            case withdraw_password_type:
            {
               auto password_condition = current_balance_record->condition.as<withdraw_with_password>();
               try {
                  if( password_condition.timeout < eval_state._current_state->now() )
                  {
                     if( !eval_state.check_signature( password_condition.payor ) )
                        FC_CAPTURE_AND_THROW( missing_signature, (password_condition.payor) );
                  }
                  else
                  {
                     if( !eval_state.check_signature( password_condition.payee ) )
                        FC_CAPTURE_AND_THROW( missing_signature, (password_condition.payee) );
                     if( claim_input_data.size() < sizeof( fc::ripemd160 ) )
                        FC_CAPTURE_AND_THROW( invalid_claim_password, (claim_input_data) );

                     auto input_password_hash = fc::ripemd160::hash( claim_input_data.data(),
                                                                     claim_input_data.size() );

                     if( password_condition.password_hash != input_password_hash )
                        FC_CAPTURE_AND_THROW( invalid_claim_password, (input_password_hash) );
                  }
               } FC_CAPTURE_AND_RETHROW( (password_condition ) )
               break;
            }

            default:
               FC_CAPTURE_AND_THROW( invalid_withdraw_condition, (current_balance_record->condition) );
         }
      }

      // update delegate vote on withdrawn account..
      if( current_balance_record->condition.asset_id == 0 && current_balance_record->condition.slate_id )
         eval_state.adjust_vote( current_balance_record->condition.slate_id, -this->amount );

      if( asset_rec->is_market_issued() )
      {
         auto yield = current_balance_record->calculate_yield( eval_state._current_state->now(),
                                                               current_balance_record->balance,
                                                               asset_rec->collected_fees,
                                                               asset_rec->current_share_supply );
         if( yield.amount > 0 )
         {
            asset_rec->collected_fees       -= yield.amount;
            current_balance_record->balance += yield.amount;
            current_balance_record->deposit_date = eval_state._current_state->now();
            eval_state.yield[current_balance_record->condition.asset_id] += yield.amount;
            eval_state._current_state->store_asset_record( *asset_rec );
         }
      }

      current_balance_record->balance -= this->amount;
      eval_state.add_balance( asset(this->amount, current_balance_record->condition.asset_id) );

      current_balance_record->last_update = eval_state._current_state->now();

      eval_state._current_state->store_balance_record( *current_balance_record );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void burn_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      if( this->amount.amount < 0 )
         FC_CAPTURE_AND_THROW( negative_deposit, (amount) );

      if( message.size() )
          FC_ASSERT( amount.asset_id == 0 );

      if( amount.asset_id == 0 )
      {
          FC_ASSERT( amount.amount >= BTS_BLOCKCHAIN_MIN_BURN_FEE, "",
                     ("amount",amount)
                     ("BTS_BLOCKCHAIN_MIN_BURN_FEE",BTS_BLOCKCHAIN_MIN_BURN_FEE) );
      }

      oasset_record asset_rec = eval_state._current_state->get_asset_record( amount.asset_id );
      FC_ASSERT( asset_rec.valid() );
      FC_ASSERT( !asset_rec->is_market_issued() );

      asset_rec->current_share_supply -= this->amount.amount;
      eval_state.sub_balance( address(), this->amount );

      eval_state._current_state->store_asset_record( *asset_rec );

      if( account_id != 0 ) // you can offer burnt offerings to God if you like... otherwise it must be an account
      {
          // TODO: support burning to any OBJECT ID not just accounts
          const oaccount_record account_rec = eval_state._current_state->get_account_record( abs( this->account_id ) );
          FC_ASSERT( account_rec.valid() );
      }
      eval_state._current_state->store_burn_record( burn_record( burn_record_key( {account_id, eval_state.trx.id()} ),
                                                                 burn_record_value( {amount,message,message_signature} ) ) );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void release_escrow_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      auto escrow_balance_record = eval_state._current_state->get_balance_record( this->escrow_id );
      FC_ASSERT( escrow_balance_record.valid() );

      if( this->amount_to_receiver < 0 )
         FC_CAPTURE_AND_THROW( negative_withdraw, (amount_to_receiver) );

      if( this->amount_to_sender < 0 )
         FC_CAPTURE_AND_THROW( negative_withdraw, (amount_to_sender) );

      if( !eval_state.check_signature( this->released_by ) )
         FC_ASSERT( false, "transaction not signed by releasor" );

      auto escrow_condition = escrow_balance_record->condition.as<withdraw_with_escrow>();
      auto total_released = uint64_t(amount_to_sender) + uint64_t(amount_to_receiver);

      FC_ASSERT( total_released <= escrow_balance_record->balance );
      FC_ASSERT( total_released >= amount_to_sender ); // check for addition overflow
      FC_ASSERT( total_released >= amount_to_receiver ); // check for addition overflow

      escrow_balance_record->balance -= total_released;
      auto asset_rec = eval_state._current_state->get_asset_record( escrow_balance_record->condition.asset_id );
      if( asset_rec->is_restricted() )
      {
         FC_ASSERT( eval_state._current_state->get_authorization( escrow_balance_record->condition.asset_id, escrow_condition.receiver ) );
      }
      if( asset_rec->is_retractable() )
      {
         if( eval_state.verify_authority( asset_rec->authority ) )
         {
            //
         }
      }

      if( escrow_condition.sender == this->released_by )
      {
         FC_ASSERT( amount_to_sender == 0 );
         FC_ASSERT( amount_to_receiver <= escrow_balance_record->balance );

         if( !eval_state.check_signature( escrow_condition.sender ) )
             FC_CAPTURE_AND_THROW( missing_signature, (escrow_condition.sender) );

         balance_record new_balance_record( escrow_condition.receiver,
                                            asset( amount_to_receiver, escrow_balance_record->asset_id() ),
                                            escrow_balance_record->slate_id() );
         auto current_receiver_balance = eval_state._current_state->get_balance_record( new_balance_record.id());

         if( current_receiver_balance )
            current_receiver_balance->balance += amount_to_receiver;
         else
            current_receiver_balance = new_balance_record;

          eval_state._current_state->store_balance_record( *current_receiver_balance );
      }
      else if( escrow_condition.receiver == this->released_by )
      {
         FC_ASSERT( amount_to_receiver == 0 );
         FC_ASSERT( amount_to_sender <= escrow_balance_record->balance );

         if( !eval_state.check_signature( escrow_condition.receiver ) )
             FC_CAPTURE_AND_THROW( missing_signature, (escrow_condition.receiver) );

         balance_record new_balance_record( escrow_condition.sender,
                                            asset( amount_to_sender, escrow_balance_record->asset_id() ),
                                            escrow_balance_record->slate_id() );
         auto current_sender_balance = eval_state._current_state->get_balance_record( new_balance_record.id());

         if( current_sender_balance )
            current_sender_balance->balance += amount_to_sender;
         else
            current_sender_balance = new_balance_record;

         eval_state._current_state->store_balance_record( *current_sender_balance );
      }
      else if( escrow_condition.escrow == this->released_by )
      {
         if( !eval_state.check_signature( escrow_condition.escrow ) )
             FC_CAPTURE_AND_THROW( missing_signature, (escrow_condition.escrow) );
         // get a balance record for the receiver, create it if necessary and deposit funds
         {
            balance_record new_balance_record( escrow_condition.receiver,
                                               asset( amount_to_receiver, escrow_balance_record->asset_id() ),
                                               escrow_balance_record->slate_id() );
            auto current_receiver_balance = eval_state._current_state->get_balance_record( new_balance_record.id());

            if( current_receiver_balance )
               current_receiver_balance->balance += amount_to_receiver;
            else
               current_receiver_balance = new_balance_record;
            eval_state._current_state->store_balance_record( *current_receiver_balance );
         }
         //  get a balance record for the sender, create it if necessary and deposit funds
         {
            balance_record new_balance_record( escrow_condition.sender,
                                               asset( amount_to_sender, escrow_balance_record->asset_id() ),
                                               escrow_balance_record->slate_id() );
            auto current_sender_balance = eval_state._current_state->get_balance_record( new_balance_record.id());

            if( current_sender_balance )
               current_sender_balance->balance += amount_to_sender;
            else
               current_sender_balance = new_balance_record;
            eval_state._current_state->store_balance_record( *current_sender_balance );
         }
      }
      else if( address() == this->released_by )
      {
         if( !eval_state.check_signature( escrow_condition.sender ) )
             FC_CAPTURE_AND_THROW( missing_signature, (escrow_condition.sender) );
         if( !eval_state.check_signature( escrow_condition.receiver ) )
             FC_CAPTURE_AND_THROW( missing_signature, (escrow_condition.receiver) );
         // get a balance record for the receiver, create it if necessary and deposit funds
         {
            balance_record new_balance_record( escrow_condition.receiver,
                                               asset( amount_to_receiver, escrow_balance_record->asset_id() ),
                                               escrow_balance_record->slate_id() );
            auto current_receiver_balance = eval_state._current_state->get_balance_record( new_balance_record.id());

            if( current_receiver_balance )
               current_receiver_balance->balance += amount_to_receiver;
            else
               current_receiver_balance = new_balance_record;
            eval_state._current_state->store_balance_record( *current_receiver_balance );
         }
         //  get a balance record for the sender, create it if necessary and deposit funds
         {
            balance_record new_balance_record( escrow_condition.sender,
                                               asset( amount_to_sender, escrow_balance_record->asset_id() ),
                                               escrow_balance_record->slate_id() );
            auto current_sender_balance = eval_state._current_state->get_balance_record( new_balance_record.id());

            if( current_sender_balance )
               current_sender_balance->balance += amount_to_sender;
            else
               current_sender_balance = new_balance_record;
            eval_state._current_state->store_balance_record( *current_sender_balance );
         }
      }
      else
      {
          FC_ASSERT( false, "not released by a party to the escrow transaction" );
      }

      eval_state._current_state->store_balance_record( *escrow_balance_record );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void update_balance_vote_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      auto current_balance_record = eval_state._current_state->get_balance_record( this->balance_id );
      FC_ASSERT( current_balance_record.valid(), "No such balance!" );
      FC_ASSERT( current_balance_record->condition.asset_id == 0, "Only BTS balances can have restricted owners." );
      FC_ASSERT( current_balance_record->condition.type == withdraw_signature_type, "Restricted owners not enabled for anything but basic balances" );


      auto last_update_secs = current_balance_record->last_update.sec_since_epoch();
      ilog("last_update_secs is: ${secs}", ("secs", last_update_secs) );

      auto balance = current_balance_record->balance;
      auto fee = BTS_BLOCKCHAIN_PRECISION / 2;
      FC_ASSERT( balance > fee );

      auto asset_rec = eval_state._current_state->get_asset_record( current_balance_record->condition.asset_id );
      if( asset_rec->is_market_issued() ) FC_ASSERT( current_balance_record->condition.slate_id == 0 );

      if( current_balance_record->condition.slate_id )
      {
          eval_state.adjust_vote( current_balance_record->condition.slate_id, -balance );
      }
      current_balance_record->balance -= balance;
      current_balance_record->last_update = eval_state._current_state->now();

      ilog("I'm storing a balance record whose last update is: ${secs}", ("secs", current_balance_record->last_update) );
      eval_state._current_state->store_balance_record( *current_balance_record );

      auto new_restricted_owner = current_balance_record->restricted_owner;
      auto new_slate = current_balance_record->condition.slate_id;


      if( this->new_restricted_owner.valid() && (this->new_restricted_owner != new_restricted_owner) )
      {
          ilog("@n new restricted owner specified and its not the existing one");
          for( const auto& owner : current_balance_record->owners() ) //eventually maybe multisig can delegate vote
          {
              if( !eval_state.check_signature( owner ) )
                  FC_CAPTURE_AND_THROW( missing_signature, (owner) );
          }
          new_restricted_owner = this->new_restricted_owner;
          new_slate = this->new_slate;
      }
      else // NOT this->new_restricted_owner.valid() || (this->new_restricted_owner == new_restricted_owner)
      {
          auto restricted_owner = current_balance_record->restricted_owner;
          /*
          FC_ASSERT( restricted_owner.valid(),
                     "Didn't specify a new restricted owner, but one currently exists." );
                     */
          ilog( "@n now: ${secs}", ("secs", eval_state._current_state->now().sec_since_epoch()) );
          ilog( "@n last update: ${secs}", ("secs", last_update_secs ) );
          FC_ASSERT( eval_state._current_state->now().sec_since_epoch() - last_update_secs
                     >= BTS_BLOCKCHAIN_VOTE_UPDATE_PERIOD_SEC,
                     "You cannot update your vote this frequently with only the voting key!" );

          if( NOT eval_state.check_signature( *restricted_owner ) )
          {
              for( const auto& owner : current_balance_record->owners() ) //eventually maybe multisig can delegate vote
              {
                  if( NOT eval_state.check_signature( owner ) )
                      FC_CAPTURE_AND_THROW( missing_signature, (owner) );
              }
          }
          new_slate = this->new_slate;
      }

      const auto owner = current_balance_record->owner();
      FC_ASSERT( owner.valid() );
      withdraw_condition new_condition( withdraw_with_signature( *owner ), 0, new_slate );
      balance_record newer_balance_record( new_condition );
      auto new_balance_record = eval_state._current_state->get_balance_record( newer_balance_record.id() );
      if( !new_balance_record.valid() )
          new_balance_record = current_balance_record;
      new_balance_record->condition = new_condition;

      if( new_balance_record->balance == 0 )
      {
         new_balance_record->deposit_date = eval_state._current_state->now();
      }
      else
      {
         fc::uint128 old_sec_since_epoch( current_balance_record->deposit_date.sec_since_epoch() );
         fc::uint128 new_sec_since_epoch( eval_state._current_state->now().sec_since_epoch() );

         fc::uint128 avg = (old_sec_since_epoch * new_balance_record->balance) + (new_sec_since_epoch * balance);
         avg /= (new_balance_record->balance + balance);

         new_balance_record->deposit_date = time_point_sec( avg.to_integer() );
      }

      new_balance_record->last_update = eval_state._current_state->now();
      new_balance_record->balance += (balance - fee);
      new_balance_record->restricted_owner = new_restricted_owner;

      eval_state.add_balance( asset(fee, 0) );

      // update delegate vote on deposited account..
      if( new_balance_record->condition.slate_id )
         eval_state.adjust_vote( new_balance_record->condition.slate_id, (balance-fee) );

      ilog("I'm storing a balance record whose last update is: ${secs}", ("secs", new_balance_record->last_update) );
      eval_state._current_state->store_balance_record( *new_balance_record );

   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void pay_fee_operation::evaluate( transaction_evaluation_state& eval_state )
   { try {
      eval_state._max_fee[this->amount.asset_id] += this->amount.amount;
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

} } // bts::blockchain
