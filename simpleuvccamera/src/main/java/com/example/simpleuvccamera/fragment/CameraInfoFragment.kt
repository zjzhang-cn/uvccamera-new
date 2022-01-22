package com.example.simpleuvccamera.fragment

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.BaseAdapter
import android.widget.Spinner
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import com.example.simpleuvccamera.R
import org.json.JSONArray
import org.json.JSONObject

class CameraInfoFragment: DialogFragment(R.layout.camera_info_dialog) {


    companion object{
        fun newInstance(formats:String):CameraInfoFragment{
            return CameraInfoFragment().apply {
                arguments = Bundle().apply {
                    putString("json_formats",formats)
                }
            }
        }
    }

    lateinit var formats: Spinner
    lateinit var resolutions: Spinner
    lateinit var json_formats:JSONArray
    var cur_sizes:JSONArray = JSONArray()
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        json_formats = JSONArray(requireArguments().getString("json_formats"))
    }
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        formats = view.findViewById(R.id.sp_formats)
        formats.adapter = object :BaseAdapter(){
            override fun getCount() = json_formats.length()

            override fun getItem(position: Int) = json_formats[position]

            override fun getItemId(position: Int) = position.toLong()

            override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
                val textView = if (convertView == null){
                    LayoutInflater.from(requireContext()).inflate(
                        android.R.layout.simple_list_item_1,parent,false)
                    as TextView
                }else{
                    convertView as TextView
                }
                val obj = getItem(position) as JSONObject
                textView.text = obj.optString("fourcc")
                return textView
            }

        }
        formats.onItemSelectedListener = object :AdapterView.OnItemSelectedListener {
            override fun onItemSelected(
                parent: AdapterView<*>?,
                view: View?,
                position: Int,
                id: Long
            ) {
                var format = json_formats[position] as JSONObject
                cur_sizes = format.getJSONArray("size") as JSONArray
                resolutions.setSelection(format.optInt("default"))
                (resolutions.adapter as BaseAdapter).notifyDataSetChanged()
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {
                cur_sizes = JSONArray()
                resolutions.setSelection(-1)
                (resolutions.adapter as BaseAdapter).notifyDataSetChanged()
            }

        }

        resolutions = view.findViewById(R.id.sp_resolution)
        resolutions.adapter = object :BaseAdapter(){
            override fun getCount() = cur_sizes.length()

            override fun getItem(position: Int) = cur_sizes[position]

            override fun getItemId(position: Int) = position.toLong()

            override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
                val textView = if (convertView == null){
                    LayoutInflater.from(requireContext()).inflate(
                        android.R.layout.simple_list_item_1,parent,false)
                            as TextView
                }else{
                    convertView as TextView
                }
                val obj = getItem(position) as String
                textView.text = obj
                return textView
            }

        }

        view.findViewById<View>(R.id.ok).setOnClickListener {
            parentFragmentManager.setFragmentResult("CameraInfo",Bundle().apply {
//                var fmtIndex = result.getInt("formatIndex")
//                var width = result.getInt("width")
//                var height = result.getInt("height")
                val cur_format_pos = formats.selectedItemPosition
                val fmt = json_formats[cur_format_pos] as JSONObject
                putInt("formatIndex",fmt.getInt("index"))
                val resolution = cur_sizes[resolutions.selectedItemPosition] as String
                putString("resolution",resolution)
            })
            dismiss()
        }
    }
}