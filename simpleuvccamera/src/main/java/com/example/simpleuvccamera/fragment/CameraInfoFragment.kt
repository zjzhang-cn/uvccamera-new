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
        fun newInstance(formats:String,width:Int,height:Int, formatIndex:Int):CameraInfoFragment{
            return CameraInfoFragment().apply {
                arguments = Bundle().apply {
                    putString("json_formats",formats)
                    putInt("width",width)
                    putInt("height",height)
                    putInt("formatIndex",formatIndex)
                }
            }
        }
    }

    lateinit var formats: Spinner
    lateinit var resolutions: Spinner
    lateinit var json_formats:JSONArray
    var cur_sizes:JSONArray = JSONArray()
    var width:Int = 0
    var height:Int = 0
    var formatIndex:Int = 0
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        json_formats = JSONArray(requireArguments().getString("json_formats"))
        width = requireArguments().getInt("width")
        height = requireArguments().getInt("height")
        formatIndex = requireArguments().getInt("formatIndex")
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
                var cur_pos = -1
                for (i in 0 until cur_sizes.length()){
                    val size = cur_sizes.getString(i)
                    if (size == "${width}x$height"){
                        cur_pos = i
                        break
                    }
                }
                if (cur_pos != -1){
                    resolutions.setSelection(cur_pos)
                }
                else{
                    resolutions.setSelection(format.optInt("default"))
                }
                (resolutions.adapter as BaseAdapter).notifyDataSetChanged()
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {
                cur_sizes = JSONArray()
                resolutions.setSelection(-1)
                (resolutions.adapter as BaseAdapter).notifyDataSetChanged()
            }

        }

        for (i in 0 until json_formats.length()){
            val fmt = json_formats.getJSONObject(i)
            if (fmt.optInt("index") == formatIndex){
                formats.setSelection(i)
                break
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