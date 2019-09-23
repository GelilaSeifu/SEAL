// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "seal/util/scalingvariant.h"
#include "seal/util/polyarithsmallmod.h"

using namespace std;

namespace seal
{
    namespace util
    {
        void multiply_plain_with_scaling_variant(
            const uint64_t *plain, size_t plain_coeff_count,
            const SEALContext::ContextData &context_data, uint64_t *destination)
        {
            auto &parms = context_data.parms();
            auto &coeff_modulus = parms.coeff_modulus();
            size_t coeff_count = parms.poly_modulus_degree();
            size_t coeff_mod_count = coeff_modulus.size();

            auto coeff_div_plain_modulus = context_data.coeff_div_plain_modulus();
            auto plain_upper_half_threshold = context_data.plain_upper_half_threshold();
            auto upper_half_increment = context_data.upper_half_increment();

            // Multiply plain by scalar coeff_div_plain_modulus_ and reposition if in upper-half.
            for (size_t i = 0; i < plain_coeff_count; i++, destination++)
            {
                if (plain[i] >= plain_upper_half_threshold)
                {
                    // Loop over primes
                    for (size_t j = 0; j < coeff_mod_count; j++)
                    {
                        unsigned long long temp[2]{ 0, 0 };
                        multiply_uint64(coeff_div_plain_modulus[j], plain[i], temp);
                        temp[1] += add_uint64(temp[0], upper_half_increment[j], 0, temp);
                        uint64_t scaled_plain_coeff = barrett_reduce_128(temp, coeff_modulus[j]);
                        destination[j * coeff_count] = add_uint_uint_mod(
                            destination[j * coeff_count], scaled_plain_coeff, coeff_modulus[j]);
                    }
                }
                else
                {
                    for (size_t j = 0; j < coeff_mod_count; j++)
                    {
                        uint64_t scaled_plain_coeff = multiply_uint_uint_mod(
                            coeff_div_plain_modulus[j], plain[i], coeff_modulus[j]);
                        destination[j * coeff_count] = add_uint_uint_mod(
                            destination[j * coeff_count], scaled_plain_coeff, coeff_modulus[j]);
                    }
                }
            }
        }

        void divide_plain_by_scaling_variant(uint64_t *plain,
            const SEALContext::ContextData &context_data, uint64_t *destination,
            MemoryPoolHandle pool)
        {
            auto &parms = context_data.parms();
            auto &coeff_modulus = parms.coeff_modulus();
            size_t coeff_count = parms.poly_modulus_degree();
            size_t coeff_mod_count = coeff_modulus.size();

            auto &base_converter = context_data.base_converter();
            auto &plain_gamma_product = base_converter->get_plain_gamma_product();
            auto &plain_gamma_array = base_converter->get_plain_gamma_array();
            auto &neg_inv_coeff = base_converter->get_neg_inv_coeff();
            auto inv_gamma = base_converter->get_inv_gamma();

            // The number of uint64 count for plain_modulus and gamma together
            size_t plain_gamma_uint64_count = 2;

            // Compute |gamma * plain|qi * ct(s)
            for (size_t i = 0; i < coeff_mod_count; i++)
            {
                multiply_poly_scalar_coeffmod(plain + (i * coeff_count), coeff_count,
                    plain_gamma_product[i], coeff_modulus[i], plain + (i * coeff_count));
            }

            // Make another temp destination to get the poly in mod {gamma U plain_modulus}
            auto tmp_dest_plain_gamma(allocate_poly(coeff_count, plain_gamma_uint64_count, pool));

            // Compute FastBConvert from q to {gamma, plain_modulus}
            base_converter->fastbconv_plain_gamma(plain, tmp_dest_plain_gamma.get(), pool);

            // Compute result multiply by coeff_modulus inverse in mod {gamma U plain_modulus}
            for (size_t i = 0; i < plain_gamma_uint64_count; i++)
            {
                multiply_poly_scalar_coeffmod(tmp_dest_plain_gamma.get() + (i * coeff_count),
                    coeff_count, neg_inv_coeff[i], plain_gamma_array[i],
                    tmp_dest_plain_gamma.get() + (i * coeff_count));
            }

            // First correct the values which are larger than floor(gamma/2)
            uint64_t gamma_div_2 = plain_gamma_array[1].value() >> 1;

            // Now compute the subtraction to remove error and perform final multiplication by
            // gamma inverse mod plain_modulus
            for (size_t i = 0; i < coeff_count; i++)
            {
                // Need correction beacuse of center mod
                if (tmp_dest_plain_gamma[i + coeff_count] > gamma_div_2)
                {
                    // Compute -(gamma - a) instead of (a - gamma)
                    tmp_dest_plain_gamma[i + coeff_count] = plain_gamma_array[1].value() -
                        tmp_dest_plain_gamma[i + coeff_count];
                    tmp_dest_plain_gamma[i + coeff_count] %= plain_gamma_array[0].value();
                    destination[i] = add_uint_uint_mod(tmp_dest_plain_gamma[i],
                        tmp_dest_plain_gamma[i + coeff_count], plain_gamma_array[0]);
                }
                // No correction needed
                else
                {
                    tmp_dest_plain_gamma[i + coeff_count] %= plain_gamma_array[0].value();
                    destination[i] = sub_uint_uint_mod(tmp_dest_plain_gamma[i],
                        tmp_dest_plain_gamma[i + coeff_count], plain_gamma_array[0]);
                }
                if (0 != destination[i])
                {
                    // Perform final multiplication by gamma inverse mod plain_modulus
                    destination[i] = multiply_uint_uint_mod(destination[i], inv_gamma,
                        plain_gamma_array[0]);
                }
            }
        }
    }
}